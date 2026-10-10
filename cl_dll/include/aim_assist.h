// Aim assist for offline play against bots.
//   Level 1: slow the crosshair down when it passes near an enemy.
//   Level 2: magnetism, the crosshair is gently pulled toward the enemy's head.
//   Level 3: auto-aim, the view locks onto the enemy's head quickly.
// Only active in a local game (loopback connection). Never active on remote servers.
#pragma once

// Register cvars and console commands. Call once from IN_Init().
void AimAssist_Init( void );

// Returns a multiplier for touch/mouse look speed (levels 1-3).
// 1.0 = no change, smaller = slower. Call each frame before applying rel_yaw / rel_pitch.
float AimAssist_GetSlowdown( const float *viewangles );

// Levels 2-3: pulls viewangles toward the best enemy.
// Call each frame after rel_yaw / rel_pitch were added and before SetViewAngles().
void AimAssist_Apply( float *viewangles, float frametime );

// Weapon spread multiplier ("spread_scale" cvar): 1.0 = normal, 0.5 = half, 0 = no spread.
// Always 1.0 when not in a local game. Used by the client side bullet prediction.
// The server side counterpart is added by scripts/patch_regamedll.py.
float AimAssist_GetSpreadScale( void );

// Weapon recoil multiplier ("recoil_scale" cvar): 1.0 = normal, 0.5 = half, 0 = no recoil.
// Always 1.0 when not in a local game. Used by the client side weapon prediction.
// The server side counterpart is added by scripts/patch_regamedll.py.
float AimAssist_GetRecoilScale( void );
