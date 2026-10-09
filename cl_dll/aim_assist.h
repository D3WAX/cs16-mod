// Aim assist for offline play against bots.
// Level 1 (this file): slow the crosshair down when it passes near an enemy.
// Only active in a local game (loopback connection). Never active on remote servers.
#pragma once

// Register cvars and console commands. Call once from IN_Init().
void AimAssist_Init( void );

// Returns a multiplier for touch/mouse look speed.
// 1.0 = no change, smaller = slower. Call each frame before applying rel_yaw / rel_pitch.
float AimAssist_GetSlowdown( const float *viewangles );
