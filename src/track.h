#pragma once

#include "common.h"

#define kNumVisibleSteps 16
#define kNumStepsDelay 1
#define kSongStartPos 0

typedef struct {
  UBYTE active_lane:2;
  UBYTE collected:1;
  UBYTE sample:5;
  UBYTE speed;
} TrackStep;

typedef struct {
  struct {
    UWORD pitch;
    UWORD count;
    BOOL in_pattern;
  } per_sample[0x20];

  UWORD selected_sample;
} TrackScore;

Status track_build();
VOID track_free();
TrackStep* track_get_steps();
ULONG track_get_length();
TrackScore* track_get_scores();
