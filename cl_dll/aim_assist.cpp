// Aim assist for offline play against bots.
//
// aim_assist 0  = off
// aim_assist 1  = slowdown near enemies
// aim_assist 2  = slowdown + magnetism (gentle pull toward the head)
// aim_assist 3  = slowdown + auto-aim (fast lock onto the head)
//
// Other cvars:
//   aim_assist_fov     cone (degrees) for slowdown and magnetism
//   aim_assist_slow    how much to slow down at the center (0.6 = 60% slower)
//   aim_assist_lockfov cone (degrees) in which level 3 acquires a target
//   aim_assist_speed   pull strength of level 3 (level 2 uses a quarter of it)
//   aim_assist_onfire  1 = levels 2 and 3 only pull while the fire button is held
//   aim_assist_headz   height of the aim point above the player origin
//
// Safety: only works when the client is connected to a local (loopback) server.
// If that cannot be confirmed, the assist stays off.

#include "hud.h"
#include "cl_util.h"
#include "kbutton.h"
#include "event_api.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "net_api.h"
#include "aim_assist.h"
#include <math.h>
#include <string.h>

#define AA_PITCH 0
#define AA_YAW   1

extern vec3_t v_origin;	// last view origin, set in view.cpp
extern kbutton_t in_attack;	// defined in input.cpp
bool CL_IsDead();

static cvar_t *aim_assist;
static cvar_t *aim_assist_fov;
static cvar_t *aim_assist_slow;
static cvar_t *aim_assist_lockfov;
static cvar_t *aim_assist_speed;
static cvar_t *aim_assist_onfire;
static cvar_t *aim_assist_headz;

static bool AimAssist_IsLocalGame( void )
{
	net_status_t st;

	if( !gEngfuncs.pNetAPI )
		return false;

	memset( &st, 0, sizeof( st ));
	gEngfuncs.pNetAPI->Status( &st );

	return st.connected && st.remote_address.type == NA_LOOPBACK;
}

// Console command: prints what the client thinks about the connection,
// so you can check on the device that "local game" detection works.
static void AimAssist_Status( void )
{
	net_status_t st;

	memset( &st, 0, sizeof( st ));
	if( gEngfuncs.pNetAPI )
		gEngfuncs.pNetAPI->Status( &st );

	gEngfuncs.Con_Printf( "aim_assist: connected=%d remote_type=%d (loopback=%d) local_game=%d\n",
		st.connected, (int)st.remote_address.type, (int)NA_LOOPBACK, AimAssist_IsLocalGame() ? 1 : 0 );
}

void AimAssist_Init( void )
{
	aim_assist = gEngfuncs.pfnRegisterVariable( "aim_assist", "0", FCVAR_ARCHIVE );
	aim_assist_fov = gEngfuncs.pfnRegisterVariable( "aim_assist_fov", "6", FCVAR_ARCHIVE );
	aim_assist_slow = gEngfuncs.pfnRegisterVariable( "aim_assist_slow", "0.6", FCVAR_ARCHIVE );
	aim_assist_lockfov = gEngfuncs.pfnRegisterVariable( "aim_assist_lockfov", "30", FCVAR_ARCHIVE );
	aim_assist_speed = gEngfuncs.pfnRegisterVariable( "aim_assist_speed", "12", FCVAR_ARCHIVE );
	aim_assist_onfire = gEngfuncs.pfnRegisterVariable( "aim_assist_onfire", "0", FCVAR_ARCHIVE );
	aim_assist_headz = gEngfuncs.pfnRegisterVariable( "aim_assist_headz", "18", FCVAR_ARCHIVE );

	gEngfuncs.pfnAddCommand( "aim_assist_status", AimAssist_Status );
}

static float AngleDiff( float a, float b )
{
	float d = a - b;

	while( d > 180.0f ) d -= 360.0f;
	while( d < -180.0f ) d += 360.0f;

	return d;
}

static float ClampF( float v, float lo, float hi )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}

// Common checks for every level. Returns false when the assist must stay off.
static bool AimAssist_Allowed( cl_entity_t **plocal, int *pmyteam )
{
	if( !aim_assist || aim_assist->value < 1.0f )
		return false;

	if( gHUD.m_iIntermission || gEngfuncs.IsSpectateOnly() || CL_IsDead() )
		return false;

	// Local game only. Remote servers: always off.
	if( !AimAssist_IsLocalGame() )
		return false;

	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if( !local )
		return false;

	const int myteam = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber;
	if( myteam != TEAM_TERRORIST && myteam != TEAM_CT )
		return false;

	*plocal = local;
	*pmyteam = myteam;
	return true;
}

// Finds the visible enemy that is closest to the crosshair, inside a cone of "cone" degrees.
// Returns the angles needed to look at the head (wantPitch / wantYaw) and the distance from the crosshair.
static bool AimAssist_FindTarget( const float *viewangles, float cone, cl_entity_t *local, int myteam,
	float *wantPitch, float *wantYaw, float *offset )
{
	float best = cone;
	bool found = false;

	for( int i = 1; i <= gEngfuncs.GetMaxClients(); i++ )
	{
		cl_entity_t *ent = gEngfuncs.GetEntityByIndex( i );

		if( !ent || !ent->player || ent == local )
			continue;

		if( ent->curstate.solid == SOLID_NOT )
			continue;

		// stale entity (not updated this frame)
		if( ent->curstate.messagenum < local->curstate.messagenum )
			continue;

		if( g_PlayerExtraInfo[i].dead )
			continue;

		const int team = g_PlayerExtraInfo[i].teamnumber;
		if(( team != TEAM_TERRORIST && team != TEAM_CT ) || team == myteam )
			continue;

		// aim point: the head. Ducked players (usehull 1) have it a bit lower.
		float headz = ClampF( aim_assist_headz->value, -20.0f, 40.0f );
		if( ent->curstate.usehull == 1 )
			headz -= 5.0f;

		float target[3] = { ent->origin[0], ent->origin[1], ent->origin[2] + headz };
		float dx = target[0] - v_origin[0];
		float dy = target[1] - v_origin[1];
		float dz = target[2] - v_origin[2];
		float dist2d = sqrtf( dx * dx + dy * dy );

		if( dist2d < 1.0f )
			continue;

		// Half-Life pitch: positive looks down
		float yaw = atan2f( dy, dx ) * ( 180.0f / (float)M_PI );
		float pitch = -atan2f( dz, dist2d ) * ( 180.0f / (float)M_PI );

		float dyaw = AngleDiff( yaw, viewangles[AA_YAW] );
		float dpitch = AngleDiff( pitch, viewangles[AA_PITCH] );
		float off = sqrtf( dyaw * dyaw + dpitch * dpitch );

		if( off >= best )
			continue;

		// Line of sight: no wall between us and the enemy's head
		pmtrace_t tr;
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
		gEngfuncs.pEventAPI->EV_PlayerTrace( v_origin, target, PM_WORLD_ONLY, -1, &tr );
		if( tr.fraction < 1.0f )
			continue;

		best = off;
		*wantPitch = pitch;
		*wantYaw = yaw;
		found = true;
	}

	*offset = best;
	return found;
}

// Levels 1-3: slow the look speed when an enemy is near the crosshair.
float AimAssist_GetSlowdown( const float *viewangles )
{
	cl_entity_t *local;
	int myteam;

	if( !AimAssist_Allowed( &local, &myteam ))
		return 1.0f;

	const float fov = ClampF( aim_assist_fov->value, 0.5f, 30.0f );
	const float slow = ClampF( aim_assist_slow->value, 0.0f, 0.8f );

	float wantPitch, wantYaw, off;
	if( !AimAssist_FindTarget( viewangles, fov, local, myteam, &wantPitch, &wantYaw, &off ))
		return 1.0f;

	// Center of the cone = strongest slowdown, edge = none
	return 1.0f - slow * ( 1.0f - off / fov );
}

// Levels 2-3: pull the view toward the enemy's head.
void AimAssist_Apply( float *viewangles, float frametime )
{
	cl_entity_t *local;
	int myteam;

	if( !AimAssist_Allowed( &local, &myteam ))
		return;

	const int level = (int)aim_assist->value;
	if( level < 2 )
		return;

	// optional: only pull while the fire button is held
	if( aim_assist_onfire->value != 0.0f && !( in_attack.state & 1 ))
		return;

	if( frametime <= 0.0f )
		return;

	float cone, speed;
	if( level >= 3 )
	{
		cone = ClampF( aim_assist_lockfov->value, 1.0f, 90.0f );
		speed = ClampF( aim_assist_speed->value, 1.0f, 60.0f );
	}
	else
	{
		cone = ClampF( aim_assist_fov->value, 0.5f, 30.0f );
		speed = ClampF( aim_assist_speed->value, 1.0f, 60.0f ) * 0.25f;
	}

	float wantPitch, wantYaw, off;
	if( !AimAssist_FindTarget( viewangles, cone, local, myteam, &wantPitch, &wantYaw, &off ))
		return;

	// Frame-rate independent smoothing: close the gap by a fraction each frame
	const float step = 1.0f - expf( -speed * frametime );

	viewangles[AA_YAW] += AngleDiff( wantYaw, viewangles[AA_YAW] ) * step;
	viewangles[AA_PITCH] += AngleDiff( wantPitch, viewangles[AA_PITCH] ) * step;

	viewangles[AA_PITCH] = ClampF( viewangles[AA_PITCH], -89.0f, 89.0f );
}
