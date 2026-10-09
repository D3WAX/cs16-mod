// Aim assist for offline play against bots (level 1: slowdown).
//
// Cvars:
//   aim_assist       0 = off, 1 = slowdown near enemies
//   aim_assist_fov   how close (in degrees) the crosshair must be to an enemy
//   aim_assist_slow  how much to slow down at the center (0.6 = 60% slower)
//
// Safety: only works when the client is connected to a local (loopback) server.
// If that cannot be confirmed, the assist stays off.

#include "hud.h"
#include "cl_util.h"
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
bool CL_IsDead();

static cvar_t *aim_assist;
static cvar_t *aim_assist_fov;
static cvar_t *aim_assist_slow;

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

float AimAssist_GetSlowdown( const float *viewangles )
{
	if( !aim_assist || aim_assist->value < 1.0f )
		return 1.0f;

	if( gHUD.m_iIntermission || gEngfuncs.IsSpectateOnly() || CL_IsDead() )
		return 1.0f;

	// Local game only. Remote servers: always off.
	if( !AimAssist_IsLocalGame() )
		return 1.0f;

	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if( !local )
		return 1.0f;

	const int myteam = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber;
	if( myteam != TEAM_TERRORIST && myteam != TEAM_CT )
		return 1.0f;

	const float fov = ClampF( aim_assist_fov->value, 0.5f, 30.0f );
	const float slow = ClampF( aim_assist_slow->value, 0.0f, 0.8f );

	float best = fov;
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

		float target[3] = { ent->origin[0], ent->origin[1], ent->origin[2] };
		float dx = target[0] - v_origin[0];
		float dy = target[1] - v_origin[1];
		float dz = target[2] - v_origin[2];
		float dist2d = sqrtf( dx * dx + dy * dy );

		if( dist2d < 1.0f )
			continue;

		// Half-Life pitch: positive looks down
		float wantYaw = atan2f( dy, dx ) * ( 180.0f / (float)M_PI );
		float wantPitch = -atan2f( dz, dist2d ) * ( 180.0f / (float)M_PI );

		float dyaw = AngleDiff( wantYaw, viewangles[AA_YAW] );
		float dpitch = AngleDiff( wantPitch, viewangles[AA_PITCH] );
		float off = sqrtf( dyaw * dyaw + dpitch * dpitch );

		if( off >= best )
			continue;

		// Line of sight: no wall between us and the enemy
		pmtrace_t tr;
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
		gEngfuncs.pEventAPI->EV_PlayerTrace( v_origin, target, PM_WORLD_ONLY, -1, &tr );
		if( tr.fraction < 1.0f )
			continue;

		best = off;
		found = true;
	}

	if( !found )
		return 1.0f;

	// Center of the cone = strongest slowdown, edge = none
	return 1.0f - slow * ( 1.0f - best / fov );
}
