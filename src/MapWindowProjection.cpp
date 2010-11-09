/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2010 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "MapWindowProjection.hpp"
#include "Screen/Layout.hpp"
#include "Waypoint/Waypoint.hpp"

#include <stdlib.h>
#include <math.h>
#include <assert.h>

MapWindowProjection::MapWindowProjection():
  WindowProjection()
{
  ScaleList[0] = fixed(500);
  ScaleList[1] = fixed(1000);
  ScaleList[2] = fixed(2000);
  ScaleList[3] = fixed(5000);
  ScaleList[4] = fixed(10000);
  ScaleList[5] = fixed(20000);
  ScaleList[6] = fixed(50000);
  ScaleList[7] = fixed(100000);
  ScaleList[8] = fixed(200000);
  ScaleList[9] = fixed(500000);
  ScaleList[10] = fixed(1000000);
  ScaleListCount = 11;
}

bool
MapWindowProjection::WaypointInScaleFilter(const Waypoint &way_point) const
{
  return (GetMapScale() <= (way_point.is_landable() ? fixed_int_constant(20000) :
                                                      fixed_int_constant(10000)));
}

fixed
MapWindowProjection::CalculateMapScale(const int scale) const
{
  assert(scale >= 0 && scale < ScaleListCount);

  return ScaleList[scale] *
    GetMapResolutionFactor() / Layout::Scale(GetScreenWidth());
}

fixed
MapWindowProjection::LimitMapScale(const fixed value) const
{
  return HaveScaleList() ? CalculateMapScale(FindMapScale(value)) : value;
}

fixed
MapWindowProjection::StepMapScale(const fixed scale, int Step) const
{
  int i = FindMapScale(scale) + Step;
  i = max(0, min(ScaleListCount - 1, i));
  return CalculateMapScale(i);
}

int
MapWindowProjection::FindMapScale(const fixed Value) const
{
  fixed BestFit;
  int BestFitIdx = 0;
  fixed DesiredScale = Value *
                       Layout::Scale(GetScreenWidth()) / GetMapResolutionFactor();

  for (int i = 0; i < ScaleListCount; i++) {
    fixed err = fabs(DesiredScale - ScaleList[i]) / DesiredScale;
    if (i == 0 || err < BestFit) {
      BestFit = err;
      BestFitIdx = i;
    }
  }

  return BestFitIdx;
}

void
MapWindowProjection::SetMapScale(const fixed x)
{
  SetScale(fixed(GetMapResolutionFactor()) / LimitMapScale(x));
}
