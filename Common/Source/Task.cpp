/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000 - 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>

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

#include "Task.h"
#include "Protection.hpp"
#include "Math/Geometry.hpp"
#include "LocalPath.hpp"
#include "Dialogs.h"
#include "Language.hpp"
#include "Settings.hpp"
#include "SettingsComputer.hpp"
#include "SettingsTask.hpp"
#include "Waypointparser.h"
#include "McReady.h"
#include "Math/Earth.hpp"
#include "LogFile.hpp"
#include "Asset.hpp"
#include "Units.hpp"
#include <math.h>
#include "Logger.h"
#include "Interface.hpp"


static int Task_saved[MAXTASKPOINTS+1];
static int active_waypoint_saved= -1;
static bool aat_enabled_saved= false;

static void BackupTask(void);
bool TaskModified=false;
bool TargetModified=false;
bool TaskAborted = false;

bool isTaskAborted() {
  return TaskAborted;
}

bool isTaskModified() {
  return TaskModified;
}

void SetTaskModified() {
  TaskModified = true;
}

bool isTargetModified() {
  return TargetModified;
}

void SetTargetModified(const bool set) {
  TargetModified = set;
  if (set) {
    SetTaskModified();
  }
}

TCHAR LastTaskFileName[MAX_PATH]= TEXT("\0");

const TCHAR* getTaskFilename() {
  return LastTaskFileName;
}

void ResetTaskWaypoint(int j) {
  task_points[j].Index = -1;
  task_stats[j].AATTargetOffsetRadius = 0.0;
  task_stats[j].AATTargetOffsetRadial = 0.0;
  task_stats[j].AATTargetLocked = false;
  task_points[j].AATSectorRadius = SectorRadius;
  task_points[j].AATCircleRadius = SectorRadius;
  task_points[j].AATStartRadial = 0;
  task_points[j].AATFinishRadial = 360;
}


void FlyDirectTo(int index, const SETTINGS_COMPUTER &settings_computer) {
  if (!CheckDeclaration())
    return;

  mutexTaskData.Lock();

  if (TaskAborted) {
    // in case we GOTO while already aborted
    ResumeAbortTask(settings_computer, -1);
  }

  if (!TaskIsTemporary()) {
    BackupTask();
  }

  TaskModified = true;
  TargetModified = true;
  ActiveTaskPoint = -1;

  AATEnabled = FALSE;

  /*  JMW disabled this so task info is preserved
  for(int j=0;j<MAXTASKPOINTS;j++)
  {
    ResetTaskWaypoint(j);
  }
  */

  task_points[0].Index = index;
  for (int i=1; i<=MAXTASKPOINTS; i++) {
    task_points[i].Index = -1;
  }
  ActiveTaskPoint = 0;
  RefreshTask(settings_computer);
  mutexTaskData.Unlock();
}


// Swaps waypoint at current index with next one.
void SwapWaypoint(int index,
		  const SETTINGS_COMPUTER &settings_computer) {
  if (!CheckDeclaration())
    return;

  mutexTaskData.Lock();
  TaskModified = true;
  TargetModified = true;
  if (index<0) {
    return;
  }
  if (index+1>= MAXTASKPOINTS-1) {
    return;
  }
  if ((task_points[index].Index != -1)&&(task_points[index+1].Index != -1)) {
    TASK_POINT tmpPoint;
    tmpPoint = task_points[index];
    task_points[index] = task_points[index+1];
    task_points[index+1] = tmpPoint;
  }
  RefreshTask(settings_computer);
  mutexTaskData.Unlock();
}


// Inserts a waypoint into the task, in the
// position of the ActiveWaypoint.  If append=true, insert at end of the
// task.
void InsertWaypoint(int index, const SETTINGS_COMPUTER &settings_computer,
		    bool append) {
  if (!CheckDeclaration())
    return;

  int i;
  ScopeLock protect(mutexTaskData);
  TaskModified = true;
  TargetModified = true;

  if ((ActiveTaskPoint<0) || !ValidTaskPoint(0)) {
    ActiveTaskPoint = 0;
    ResetTaskWaypoint(ActiveTaskPoint);
    task_points[ActiveTaskPoint].Index = index;
    RefreshTask(settings_computer);
    return;
  }

  if (ValidTaskPoint(MAXTASKPOINTS-1)) {
    // No room for any more task points!
    MessageBoxX(
      gettext(TEXT("Too many waypoints in task!")),
      gettext(TEXT("Insert Waypoint")),
      MB_OK|MB_ICONEXCLAMATION);
    return;
  }

  int indexInsert = max(ActiveTaskPoint,0);
  if (append) {
    for (i=indexInsert; i<MAXTASKPOINTS-2; i++) {
      if (task_points[i+1].Index<0) {
	ResetTaskWaypoint(i+1);
	task_points[i+1].Index = index;
	break;
      }
    }
  } else {
    // Shuffle ActiveWaypoint and all later task points
    // to the right by one position
    for (i=MAXTASKPOINTS-1; i>indexInsert; i--) {
      task_points[i] = task_points[i-1];
    }
    // Insert new point and update task details
    ResetTaskWaypoint(indexInsert);
    task_points[indexInsert].Index = index;
  }

  RefreshTask(settings_computer);
}

// Create a default task to home at startup if no task is present
void DefaultTask(const SETTINGS_COMPUTER &settings_computer) {
  mutexTaskData.Lock();
  TaskModified = true;
  TargetModified = true;
  if ((task_points[0].Index == -1)||(ActiveTaskPoint==-1)) {
    if (settings_computer.HomeWaypoint != -1) {
      task_points[0].Index = settings_computer.HomeWaypoint;
      ActiveTaskPoint = 0;
    }
  }
  RefreshTask(settings_computer);
  mutexTaskData.Unlock();
}


// RemoveTaskpoint removes a single waypoint
// from the current task.  index specifies an entry
// in the task_points[] array - NOT a waypoint index.
//
// If you call this function, you MUST deal with
// correctly setting ActiveTaskPoint yourself!
void RemoveTaskPoint(int index, 
		     const SETTINGS_COMPUTER &settings_computer) {
  if (!CheckDeclaration())
    return;

  int i;

  if (index < 0 || index >= MAXTASKPOINTS) {
    return; // index out of bounds
  }

  mutexTaskData.Lock();
  TaskModified = true;
  TargetModified = true;

  if (task_points[index].Index == -1) {
    mutexTaskData.Unlock();
    return; // There's no WP at this location
  }

  // Shuffle all later taskpoints to the left to
  // fill the gap
  for (i=index; i<MAXTASKPOINTS-1; ++i) {
    task_points[i] = task_points[i+1];
  }
  task_points[MAXTASKPOINTS-1].Index = -1;
  task_stats[MAXTASKPOINTS-1].AATTargetOffsetRadius= 0.0;

  RefreshTask(settings_computer);
  mutexTaskData.Unlock();

}


// Index specifies a waypoint in the WP list
// It won't necessarily be a waypoint that's
// in the task
void RemoveWaypoint(int index,
		    const SETTINGS_COMPUTER &settings_computer) {
  int i;

  if (!CheckDeclaration())
    return;

  if (ActiveTaskPoint<0) {
    return; // No waypoint to remove
  }

  // Check to see whether selected WP is actually
  // in the task list.
  // If not, we'll ask the user if they want to remove
  // the currently active task point.
  // If the WP is in the task multiple times then we'll
  // remove the first instance after (or including) the
  // active WP.
  // If they're all before the active WP then just remove
  // the nearest to the active WP

  mutexTaskData.Lock();
  TaskModified = true;
  TargetModified = true;

  // Search forward first
  i = ActiveTaskPoint;
  while ((i < MAXTASKPOINTS) && (task_points[i].Index != index)) {
    ++i;
  }

  if (i < MAXTASKPOINTS) {
    // Found WP, so remove it
    RemoveTaskPoint(i, settings_computer);

    if (task_points[ActiveTaskPoint].Index == -1) {
      // We've just removed the last task point and it was
      // active at the time
      ActiveTaskPoint--;
    }

  } else {
    // Didn't find WP, so search backwards

    i = ActiveTaskPoint;
    do {
      --i;
    } while (i >= 0 && task_points[i].Index != index);

    if (i >= 0) {
      // Found WP, so remove it
      RemoveTaskPoint(i, settings_computer);
      ActiveTaskPoint--;

    } else {
      // WP not found, so ask user if they want to
      // remove the active WP
      mutexTaskData.Unlock();
      int ret = MessageBoxX(
        gettext(TEXT("Chosen Waypoint not in current task.\nRemove active WayPoint?")),
        gettext(TEXT("Remove Waypoint")),
        MB_YESNO|MB_ICONQUESTION);
      mutexTaskData.Lock();

      if (ret == IDYES) {
        RemoveTaskPoint(ActiveTaskPoint, settings_computer);
        if (task_points[ActiveTaskPoint].Index == -1) {
          // Active WayPoint was last in the list so is currently
          // invalid.
          ActiveTaskPoint--;
        }
      }
    }
  }
  RefreshTask(settings_computer);
  mutexTaskData.Unlock();

}


void ReplaceWaypoint(int index,
		     const SETTINGS_COMPUTER &settings_computer) {
  if (!CheckDeclaration())
    return;

  mutexTaskData.Lock();
  TaskModified = true;
  TargetModified = true;

  // ARH 26/06/05 Fixed array out-of-bounds bug
  if (ActiveTaskPoint>=0) {
    ResetTaskWaypoint(ActiveTaskPoint);
    task_points[ActiveTaskPoint].Index = index;
  } else {

    // Insert a new waypoint since there's
    // nothing to replace
    ActiveTaskPoint=0;
    ResetTaskWaypoint(ActiveTaskPoint);
    task_points[ActiveTaskPoint].Index = index;
  }
  RefreshTask(settings_computer);
  mutexTaskData.Unlock();
}

static void CalculateAATTaskSectors(const NMEA_INFO &gps_info);


void RefreshTask(const SETTINGS_COMPUTER &settings_computer) {
  double lengthtotal = 0.0;
  int i;

  mutexTaskData.Lock();
  if ((ActiveTaskPoint<0)&&(task_points[0].Index>=0)) {
    ActiveTaskPoint=0;
  }

  // Only need to refresh info where the removal happened
  // as the order of other taskpoints hasn't changed
  for (i=0; i<MAXTASKPOINTS; i++) {
    if (!ValidTaskPoint(i)) {
      task_points[i].Index = -1;
    } else {
      RefreshTaskWaypoint(i);
      lengthtotal += task_points[i].Leg;
    }
  }
  if (lengthtotal>0) {
    for (i=0; i<MAXTASKPOINTS; i++) {
      if (ValidTaskPoint(i)) {
	RefreshTaskWaypoint(i);
	task_stats[i].LengthPercent = task_points[i].Leg/lengthtotal;
	if (!ValidTaskPoint(i+1)) {
          // this is the finish waypoint
	  task_stats[i].AATTargetOffsetRadius = 0.0;
	  task_stats[i].AATTargetOffsetRadial = 0.0;
	  task_stats[i].AATTargetLat = WayPointList[task_points[i].Index].Latitude;
	  task_stats[i].AATTargetLon = WayPointList[task_points[i].Index].Longitude;
	}
      }
    }
  }

  // Determine if a waypoint is in the task
  if (WayPointList) {
    for (i=0; i< (int)NumberOfWayPoints; i++) {
      WayPointList[i].InTask = false;
      if ((WayPointList[i].Flags & HOME) == HOME) {
        WayPointList[i].InTask = true;
      }
    }
    if (settings_computer.HomeWaypoint>=0) {
      WayPointList[settings_computer.HomeWaypoint].InTask = true;
    }
    for (i=0; i<MAXTASKPOINTS; i++) {
      if (ValidTaskPoint(i)) {
        WayPointList[task_points[i].Index].InTask = true;
      }
    }
    if (EnableMultipleStartPoints) {
      for (i=0; i<MAXSTARTPOINTS; i++) {
        if (ValidWayPoint(task_start_points[i].Index) 
	    && task_start_stats[i].Active) {
          WayPointList[task_start_points[i].Index].InTask = true;
        }
      }
    }
  }

  CalculateTaskSectors();
  CalculateAATTaskSectors(XCSoarInterface::Basic());
  mutexTaskData.Unlock();
}


void RotateStartPoints(const SETTINGS_COMPUTER &settings_computer) {
  if (ActiveTaskPoint>0) return;
  if (!EnableMultipleStartPoints) return;

  mutexTaskData.Lock();

  int found = -1;
  int imax = 0;
  for (int i=0; i<MAXSTARTPOINTS; i++) {
    if (task_start_stats[i].Active && ValidWayPoint(task_start_points[i].Index)) {
      if (task_points[0].Index == task_start_points[i].Index) {
        found = i;
      }
      imax = i;
    }
  }
  found++;
  if (found>imax) {
    found = 0;
  }
  if (ValidWayPoint(task_start_points[found].Index)) {
    task_points[0].Index = task_start_points[found].Index;
  }

  RefreshTask(settings_computer);
  mutexTaskData.Unlock();
}


void CalculateTaskSectors(void)
{
  int i;
  double SectorAngle, SectorSize, SectorBearing;

  mutexTaskData.Lock();

  if (EnableMultipleStartPoints) {
    for(i=0;i<MAXSTARTPOINTS-1;i++) {
      if (task_start_stats[i].Active && ValidWayPoint(task_start_points[i].Index)) {
	if (StartLine==2) {
          SectorAngle = 45+90;
        } else {
          SectorAngle = 90;
        }
        SectorSize = StartRadius;
        SectorBearing = task_start_points[i].OutBound;

        FindLatitudeLongitude(WayPointList[task_start_points[i].Index].Latitude,
                              WayPointList[task_start_points[i].Index].Longitude,
                              SectorBearing + SectorAngle, SectorSize,
                              &task_start_points[i].SectorStartLat,
                              &task_start_points[i].SectorStartLon);

        FindLatitudeLongitude(WayPointList[task_start_points[i].Index].Latitude,
                              WayPointList[task_start_points[i].Index].Longitude,
                              SectorBearing - SectorAngle, SectorSize,
                              &task_start_points[i].SectorEndLat,
                              &task_start_points[i].SectorEndLon);
      }
    }
  }

  for(i=0;i<=MAXTASKPOINTS-1;i++)
    {
      if((task_points[i].Index >=0))
	{
	  if ((task_points[i+1].Index >=0)||(i==MAXTASKPOINTS-1)) {

	    if(i == 0)
	      {
		// start line
		if (StartLine==2) {
		  SectorAngle = 45+90;
		} else {
		  SectorAngle = 90;
		}
		SectorSize = StartRadius;
		SectorBearing = task_points[i].OutBound;
	      }
	    else
	      {
		// normal turnpoint sector
		SectorAngle = 45;
		if (SectorType == 2) {
		  SectorSize = 10000; // German DAe 0.5/10
		} else {
		  SectorSize = SectorRadius;  // FAI sector
		}
		SectorBearing = task_points[i].Bisector;
	      }
	  } else {
	    // finish line
	    if (FinishLine==2) {
	      SectorAngle = 45;
	    } else {
	      SectorAngle = 90;
	    }
	    SectorSize = FinishRadius;
	    SectorBearing = task_points[i].InBound;

            // no clearing of this, so default can happen with ClearTask
            // task_points[i].AATCircleRadius = 0;
            // task_points[i].AATSectorRadius = 0;

	  }

          FindLatitudeLongitude(WayPointList[task_points[i].Index].Latitude,
                                WayPointList[task_points[i].Index].Longitude,
                                SectorBearing + SectorAngle, SectorSize,
                                &task_points[i].SectorStartLat,
                                &task_points[i].SectorStartLon);

          FindLatitudeLongitude(WayPointList[task_points[i].Index].Latitude,
                                WayPointList[task_points[i].Index].Longitude,
                                SectorBearing - SectorAngle, SectorSize,
                                &task_points[i].SectorEndLat,
                                &task_points[i].SectorEndLon);

          if (!AATEnabled) {
            task_points[i].AATStartRadial  =
              AngleLimit360(SectorBearing - SectorAngle);
            task_points[i].AATFinishRadial =
              AngleLimit360(SectorBearing + SectorAngle);
          }

	}
    }
  mutexTaskData.Unlock();
}


double AdjustAATTargets(double desired) {
  int i, istart, inum;
  double av=0;
  istart = max(1,ActiveTaskPoint);
  inum=0;

  mutexTaskData.Lock();
  for(i=istart;i<MAXTASKPOINTS-1;i++)
    {
      if(ValidTaskPoint(i)&&ValidTaskPoint(i+1) && !task_stats[i].AATTargetLocked)
	{
          task_stats[i].AATTargetOffsetRadius = max(-1,min(1,
                                          task_stats[i].AATTargetOffsetRadius));
	  av += task_stats[i].AATTargetOffsetRadius;
	  inum++;
	}
    }
  if (inum>0) {
    av/= inum;
  }
  if (fabs(desired)>1.0) {
    // don't adjust, just retrieve.
    goto OnExit;
  }

  // TODO accuracy: Check here for true minimum distance between
  // successive points (especially second last to final point)

  // Do this with intersection tests

  desired = (desired+1.0)/2.0; // scale to 0,1
  av = (av+1.0)/2.0; // scale to 0,1

  for(i=istart;i<MAXTASKPOINTS-1;i++)
    {
      if((task_points[i].Index >=0)
	 &&(task_points[i+1].Index >=0) 
	 && !task_stats[i].AATTargetLocked)
	{
	  double d = (task_stats[i].AATTargetOffsetRadius+1.0)/2.0;
          // scale to 0,1

          if (av>0.01) {
            d = desired;
	    // 20080615 JMW
	    // was (desired/av)*d;
	    // now, we don't want it to be proportional
          } else {
            d = desired;
          }
          d = min(1.0, max(d, 0))*2.0-1.0;
          task_stats[i].AATTargetOffsetRadius = d;
	}
    }
 OnExit:
  mutexTaskData.Unlock();
  return av;
}


void CalculateAATTaskSectors(const NMEA_INFO &gps_info)
{
  int i;
  int awp = ActiveTaskPoint;

  if(AATEnabled == FALSE)
    return;

  double latitude = gps_info.Latitude;
  double longitude = gps_info.Longitude;

  mutexTaskData.Lock();

  task_stats[0].AATTargetOffsetRadius = 0.0;
  task_stats[0].AATTargetOffsetRadial = 0.0;
  if (task_points[0].Index>=0) {
    task_stats[0].AATTargetLat = WayPointList[task_points[0].Index].Latitude;
    task_stats[0].AATTargetLon = WayPointList[task_points[0].Index].Longitude;
  }

  for(i=1;i<MAXTASKPOINTS;i++) {
    if(ValidTaskPoint(i)) {
      if (!ValidTaskPoint(i+1)) {
        // This must be the final waypoint, so it's not an AAT OZ
        task_stats[i].AATTargetLat = WayPointList[task_points[i].Index].Latitude;
        task_stats[i].AATTargetLon = WayPointList[task_points[i].Index].Longitude;
        continue;
      }

      if(task_points[i].AATType == SECTOR) {
        FindLatitudeLongitude (WayPointList[task_points[i].Index].Latitude,
                                 WayPointList[task_points[i].Index].Longitude,
                               task_points[i].AATStartRadial ,
                               task_points[i].AATSectorRadius ,
                               &task_points[i].AATStartLat,
                               &task_points[i].AATStartLon);

        FindLatitudeLongitude (WayPointList[task_points[i].Index].Latitude,
                               WayPointList[task_points[i].Index].Longitude,
                               task_points[i].AATFinishRadial ,
                               task_points[i].AATSectorRadius,
                               &task_points[i].AATFinishLat,
                               &task_points[i].AATFinishLon);
      }

      // JMWAAT: if locked, don't move it
      if (i<awp) {
        // only update targets for current/later waypoints
        continue;
      }

      task_stats[i].AATTargetOffsetRadius =
        min(1.0, max(task_stats[i].AATTargetOffsetRadius,-1.0));

      task_stats[i].AATTargetOffsetRadial =
        min(90, max(-90, task_stats[i].AATTargetOffsetRadial));

      double targetbearing;
      double targetrange;

      targetbearing = AngleLimit360(task_points[i].Bisector+task_stats[i].AATTargetOffsetRadial);

      if(task_points[i].AATType == SECTOR) {

        //AATStartRadial
        //AATFinishRadial

        targetrange = ((task_stats[i].AATTargetOffsetRadius+1.0)/2.0);

        double aatbisector = HalfAngle(task_points[i].AATStartRadial,
                                       task_points[i].AATFinishRadial);

        if (fabs(AngleLimit180(aatbisector-targetbearing))>90) {
          // bisector is going away from sector
          targetbearing = Reciprocal(targetbearing);
          targetrange = 1.0-targetrange;
        }
        if (!AngleInRange(task_points[i].AATStartRadial,
                          task_points[i].AATFinishRadial,
                          targetbearing,true)) {

          // Bisector is not within AAT sector, so
          // choose the closest radial as the target line

          if (fabs(AngleLimit180(task_points[i].AATStartRadial-targetbearing))
              <fabs(AngleLimit180(task_points[i].AATFinishRadial-targetbearing))) {
            targetbearing = task_points[i].AATStartRadial;
          } else {
            targetbearing = task_points[i].AATFinishRadial;
          }
        }

        targetrange*= task_points[i].AATSectorRadius;

      } else {
        targetrange = task_stats[i].AATTargetOffsetRadius
          *task_points[i].AATCircleRadius;
      }

      // TODO accuracy: if i=awp and in sector, range parameter needs to
      // go from current aircraft position to projection of target
      // out to the edge of the sector

      if (InAATTurnSector(longitude, latitude, i) && (awp==i) &&
          !task_stats[i].AATTargetLocked) {

        // special case, currently in AAT sector/cylinder

        double dist;
        double qdist;
        double bearing;

        // find bearing from last target through current aircraft position with offset
        DistanceBearing(task_stats[i-1].AATTargetLat,
                        task_stats[i-1].AATTargetLon,
                        latitude,
                        longitude,
                        &qdist, &bearing);

        bearing = AngleLimit360(bearing+task_stats[i].AATTargetOffsetRadial);

        dist = ((task_stats[i].AATTargetOffsetRadius+1)/2.0)*
          FindInsideAATSectorDistance(latitude, longitude, i, bearing);

        // if (dist+qdist>aatdistance.LegDistanceAchieved(awp)) {
        // JMW: don't prevent target from being closer to the aircraft
        // than the best achieved, so can properly plan arrival time

        FindLatitudeLongitude (latitude,
                               longitude,
                               bearing,
                               dist,
                               &task_stats[i].AATTargetLat,
                               &task_stats[i].AATTargetLon);

        TargetModified = true;

        // }

      } else {

        FindLatitudeLongitude (WayPointList[task_points[i].Index].Latitude,
                               WayPointList[task_points[i].Index].Longitude,
                               targetbearing,
                               targetrange,
                               &task_stats[i].AATTargetLat,
                               &task_stats[i].AATTargetLon);
        TargetModified = true;

      }
    }
  }

  CalculateAATIsoLines();
  if (!targetManipEvent.test()) {
    TargetModified = false;
    // allow target dialog to detect externally changed targets
  }

  mutexTaskData.Unlock();
}


//////////////


void RefreshTaskWaypoint(int i) {
  if(i==0)
    {
      task_points[i].Leg = 0;
      task_points[i].InBound = 0;
    }
  else
    {
      DistanceBearing(WayPointList[task_points[i-1].Index].Latitude,
                      WayPointList[task_points[i-1].Index].Longitude,
                      WayPointList[task_points[i].Index].Latitude,
                      WayPointList[task_points[i].Index].Longitude,
                      &task_points[i].Leg,
                      &task_points[i].InBound);

      task_points[i-1].OutBound = task_points[i].InBound;
      task_points[i-1].Bisector = BiSector(task_points[i-1].InBound,task_points[i-1].OutBound);
      if (i==1) {
        if (EnableMultipleStartPoints) {
          for (int j=0; j<MAXSTARTPOINTS; j++) {
            if ((task_start_points[j].Index != -1)
		&&(task_start_stats[j].Active)) {
              DistanceBearing(WayPointList[task_start_points[j].Index].Latitude,
                              WayPointList[task_start_points[j].Index].Longitude,
                              WayPointList[task_points[i].Index].Latitude,
                              WayPointList[task_points[i].Index].Longitude,
                              NULL, &task_start_points[j].OutBound);
            }
          }
        }
      }
    }
}


static int FindOrAddWaypoint(WAYPOINT *read_waypoint) {
  // this is an invalid pointer!
  read_waypoint->Details = 0;
  read_waypoint->Name[NAME_SIZE-1] = 0; // prevent overrun if data is bogus

  int waypoint_index = FindMatchingWaypoint(read_waypoint);
  if (waypoint_index == -1) {
    // waypoint not found, so add it!

    // TODO bug: Set WAYPOINTFILECHANGED so waypoints get saved?

    WAYPOINT* new_waypoint = GrowWaypointList();
    if (!new_waypoint) {
      // error, can't allocate!
      return false;
    }
    memcpy(new_waypoint, read_waypoint, sizeof(WAYPOINT));
    waypoint_index = NumberOfWayPoints-1;
  }
  return waypoint_index;
}


static bool LoadTaskWaypoints(FILE *file) {
  WAYPOINT read_waypoint;

  int i;
  for(i=0;i<MAXTASKPOINTS;i++) {
    if (fread(&read_waypoint, sizeof(read_waypoint), 1, file) != 1) {
      return false;
    }
    if (task_points[i].Index != -1) {
      task_points[i].Index = FindOrAddWaypoint(&read_waypoint);
    }
  }
  for(i=0;i<MAXSTARTPOINTS;i++) {
    if (fread(&read_waypoint, sizeof(read_waypoint), 1, file) != 1) {
      return false;
    }
    if (task_start_points[i].Index != -1) {
      task_start_points[i].Index = FindOrAddWaypoint(&read_waypoint);
    }
  }
  // managed to load everything
  return true;
}

#define  BINFILEMAGICNUMBER     0x5cf77fcc

// loads a new task from scratch.
void LoadNewTask(const TCHAR *szFileName,
		 const SETTINGS_COMPUTER &settings_computer)
{
  TASK_POINT Temp;
  START_POINT STemp;
  int i;
  bool TaskInvalid = false;
  bool WaypointInvalid = false;
  bool TaskLoaded = false;
  unsigned magic = 0;

  mutexTaskData.Lock();

  ActiveTaskPoint = -1;
  for(i=0;i<MAXTASKPOINTS;i++)
    {
      task_points[i].Index = -1;
    }

  FILE *file = _tfopen(szFileName, _T("rb"));
  if(file != NULL)
    {
      if (fread(&magic, sizeof(magic), 1, file) != 1) {
	TaskInvalid = true;
      } else if (magic != BINFILEMAGICNUMBER) {
	TaskInvalid = true;
      } else {

      // Defaults
      int   old_StartLine    = StartLine;
      int   old_SectorType   = SectorType;
      DWORD old_SectorRadius = SectorRadius;
      DWORD old_StartRadius  = StartRadius;
      int   old_AutoAdvance  = AutoAdvance;
      double old_AATTaskLength = AATTaskLength;
      BOOL   old_AATEnabled  = AATEnabled;
      DWORD  old_FinishRadius = FinishRadius;
      int    old_FinishLine = FinishLine;
      bool   old_EnableMultipleStartPoints = EnableMultipleStartPoints;

      TaskLoaded = true;

      for(i=0;i<MAXTASKPOINTS;i++)
        {
          if (fread(&Temp, sizeof(Temp), 1, file) != 1)
            {
              TaskInvalid = true;
              break;
            }
	  memcpy(&task_points[i],&Temp, sizeof(TASK_POINT));

          if(!ValidWayPoint(Temp.Index) && (Temp.Index != -1)) {
            // Task is only invalid here if the index is out of range
            // of the waypoints and not equal to -1.
            // (Because -1 indicates a null task item)
	    WaypointInvalid = true;
	  }

        }

      if (!TaskInvalid) {

        if (fread(&AATEnabled, sizeof(AATEnabled), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&AATTaskLength, sizeof(AATTaskLength), 1, file) != 1) {
          TaskInvalid = true;
        }

	// ToDo review by JW

	// 20060521:sgi added additional task parameters
        if (fread(&FinishRadius, sizeof(FinishRadius), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&FinishLine, sizeof(FinishLine), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&StartRadius, sizeof(StartRadius), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&StartLine, sizeof(StartLine), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&SectorType, sizeof(SectorType), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&SectorRadius, sizeof(SectorRadius), 1, file) != 1) {
          TaskInvalid = true;
        }
        if (fread(&AutoAdvance, sizeof(AutoAdvance), 1, file) != 1) {
          TaskInvalid = true;
        }

        if (fread(&EnableMultipleStartPoints,
                  sizeof(EnableMultipleStartPoints), 1, file) != 1) {
          TaskInvalid = true;
        }

        for(i=0;i<MAXSTARTPOINTS;i++)
        {
          if (fread(&STemp, sizeof(STemp), 1, file) != 1) {
            TaskInvalid = true;
            break;
          }

          if(ValidWayPoint(STemp.Index) || (STemp.Index==-1)) {
            memcpy(&task_start_points[i],&STemp, sizeof(START_POINT));
          } else {
	    WaypointInvalid = true;
	  }
        }

        //// search for waypoints...
        if (!TaskInvalid) {
          if (!LoadTaskWaypoints(file) && WaypointInvalid) {
            // couldn't lookup the waypoints in the file and we know there are invalid waypoints
            TaskInvalid = true;
          }
        }

      }

      if (TaskInvalid) {
        StartLine = old_StartLine;
        SectorType = old_SectorType;
        SectorRadius = old_SectorRadius;
        StartRadius = old_StartRadius;
        AutoAdvance = old_AutoAdvance;
        AATTaskLength = old_AATTaskLength;
        AATEnabled = old_AATEnabled;
        FinishRadius = old_FinishRadius;
        FinishLine = old_FinishLine;
        EnableMultipleStartPoints = old_EnableMultipleStartPoints;
      }
      }

      fclose(file);

  } else {
    TaskInvalid = true;
  }

  if (TaskInvalid) {
    ClearTask();
  }

  RefreshTask(settings_computer);

  if (!ValidTaskPoint(0)) {
    ActiveTaskPoint = 0;
  }

  mutexTaskData.Unlock();

  if (TaskInvalid && TaskLoaded) {
    MessageBoxX(
      gettext(TEXT("Error in task file!")),
      gettext(TEXT("Load task")),
      MB_OK|MB_ICONEXCLAMATION);
  } else {
    TaskModified = false;
    TargetModified = false;
    _tcscpy(LastTaskFileName, szFileName);
  }

}


void ClearTask(void) {
  mutexTaskData.Lock();

  memset( &(task_points), 0, sizeof(Task_t));
  memset( &(task_start_points), 0, sizeof(Start_t));

  TaskModified = true;
  TargetModified = true;
  LastTaskFileName[0] = _T('\0');
  ActiveTaskPoint = -1;
  int i;
  for(i=0;i<MAXTASKPOINTS;i++) {
    task_points[i].Index = -1;
    task_points[i].AATSectorRadius = SectorRadius; // JMW added default
    task_points[i].AATCircleRadius = SectorRadius; // JMW added default
    task_stats[i].AATTargetOffsetRadial = 0;
    task_stats[i].AATTargetOffsetRadius = 0;
    task_stats[i].AATTargetLocked = false;
    for (int j=0; j<MAXISOLINES; j++) {
      task_stats[i].IsoLine_valid[j] = false;
    }
    Task_saved[i] = task_points[i].Index;
  }
  for (i=0; i<MAXSTARTPOINTS; i++) {
    task_start_points[i].Index = -1;
  }
  mutexTaskData.Unlock();
}


bool ValidWayPoint(const int i) {
  ScopeLock protect(mutexTaskData);
  if ((!WayPointList)||(i<0)||(i>=(int)NumberOfWayPoints)) {
    return false;
  } else {
    return true;
  }
}

bool ValidTask()  {
  return ValidTaskPoint(ActiveTaskPoint);
}

bool ValidTaskPoint(const int i) {
  ScopeLock protect(mutexTaskData);
  if ((i<0) || (i>= MAXTASKPOINTS))
    return false;
  else if (!ValidWayPoint(task_points[i].Index))
    return false;
  else 
    return true;
}

void SaveTask(const TCHAR *szFileName)
{
  if (!WayPointList) return; // this should never happen, but just to be safe...

  mutexTaskData.Lock();

  FILE *file = _tfopen(szFileName, _T("wb"));
  if (file != NULL) {
    unsigned magic = BINFILEMAGICNUMBER;
    fwrite(&magic, sizeof(magic), 1, file);
    fwrite(&task_points[0], sizeof(task_points[0]), MAXTASKPOINTS, file);
    fwrite(&AATEnabled, sizeof(AATEnabled), 1, file);
    fwrite(&AATTaskLength, sizeof(AATTaskLength), 1, file);

    // 20060521:sgi added additional task parameters
    fwrite(&FinishRadius, sizeof(FinishRadius), 1, file);
    fwrite(&FinishLine, sizeof(FinishLine), 1, file);
    fwrite(&StartRadius, sizeof(StartRadius), 1, file);
    fwrite(&StartLine, sizeof(StartLine), 1, file);
    fwrite(&SectorType, sizeof(SectorType), 1, file);
    fwrite(&SectorRadius, sizeof(SectorRadius), 1, file);
    fwrite(&AutoAdvance, sizeof(AutoAdvance), 1, file);

    fwrite(&EnableMultipleStartPoints,
           sizeof(EnableMultipleStartPoints), 1, file);
    fwrite(&task_start_points[0], sizeof(task_start_points[0]), MAXSTARTPOINTS, file);

    // JMW added writing of waypoint data, in case it's missing
    int i;
    for(i=0;i<MAXTASKPOINTS;i++) {
      if (ValidWayPoint(task_points[i].Index)) {
        fwrite(&WayPointList[task_points[i].Index],
               sizeof(WayPointList[task_points[i].Index]), 1, file);
      } else {
        // dummy data..
        fwrite(&WayPointList[0], sizeof(WayPointList[0]), 1, file);
      }
    }
    for(i=0;i<MAXSTARTPOINTS;i++) {
      if (ValidWayPoint(task_start_points[i].Index)) {
        fwrite(&WayPointList[task_start_points[i].Index],
               sizeof(WayPointList[task_start_points[i].Index]), 1, file);
      } else {
        // dummy data..
        fwrite(&WayPointList[0], sizeof(WayPointList[0]), 1, file);
      }
    }

    fclose(file);
    TaskModified = false; // task successfully saved
    TargetModified = false;
    _tcscpy(LastTaskFileName, szFileName);

  } else {

    MessageBoxX(
                gettext(TEXT("Error in saving task!")),
                gettext(TEXT("Save task")),
                MB_OK|MB_ICONEXCLAMATION);
  }
  mutexTaskData.Unlock();
}


double FindInsideAATSectorDistance_old(double latitude,
                                       double longitude,
                                       int taskwaypoint,
                                       double course_bearing,
                                       double p_found) {
  bool t_in_sector;
  double delta;
  double max_distance;
  if(task_points[taskwaypoint].AATType == SECTOR) {
    max_distance = task_points[taskwaypoint].AATSectorRadius*2;
  } else {
    max_distance = task_points[taskwaypoint].AATCircleRadius*2;
  }
  delta = max(250.0, max_distance/40.0);

  double t_distance = p_found;
  double t_distance_inside;

  do {
    double t_lat, t_lon;
    t_distance_inside = t_distance;
    t_distance += delta;

    FindLatitudeLongitude(latitude, longitude,
                          course_bearing, t_distance,
                          &t_lat,
                          &t_lon);

    t_in_sector = InAATTurnSector(t_lon,
                                  t_lat,
                                  taskwaypoint);

  } while (t_in_sector);

  return t_distance_inside;
}

/////////////////


double FindInsideAATSectorDistance(double latitude,
                                   double longitude,
                                   int taskwaypoint,
                                   double course_bearing,
                                   double p_found) {

  double max_distance;
  if(task_points[taskwaypoint].AATType == SECTOR) {
    max_distance = task_points[taskwaypoint].AATSectorRadius;
  } else {
    max_distance = task_points[taskwaypoint].AATCircleRadius;
  }

  // Do binary bounds search for longest distance within sector

  double delta = max_distance;
  double t_distance_lower = p_found;
  double t_distance = p_found+delta*2;
  int steps = 0;
  do {

    double t_lat, t_lon;
    FindLatitudeLongitude(latitude, longitude,
                          course_bearing, t_distance,
                          &t_lat, &t_lon);

    if (InAATTurnSector(t_lon, t_lat, taskwaypoint)) {
      t_distance_lower = t_distance;
      // ok, can go further
      t_distance += delta;
    } else {
      t_distance -= delta;
    }
    delta /= 2.0;
  } while ((delta>5.0)&&(steps++<20));

  return t_distance_lower;
}


double FindInsideAATSectorRange(double latitude,
                                double longitude,
                                int taskwaypoint,
                                double course_bearing,
                                double p_found) {

  double t_distance = FindInsideAATSectorDistance(latitude, longitude, taskwaypoint,
                                                  course_bearing, p_found);
  return (p_found /
          max(1,t_distance))*2-1;
}


/////////////////

double DoubleLegDistance(int taskwaypoint,
                         double longitude,
                         double latitude) {

#if 0
  double d0;
  double d1;
  if (taskwaypoint>0) {
    DistanceBearing(task_stats[taskwaypoint-1].AATTargetLat,
                    task_stats[taskwaypoint-1].AATTargetLon,
                    latitude,
                    longitude,
                    &d0, NULL);
  } else {
    d0 = 0;
  }

  DistanceBearing(latitude,
                  longitude,
                  task_stats[taskwaypoint+1].AATTargetLat,
                  task_stats[taskwaypoint+1].AATTargetLon,
                  &d1, NULL);
  return d0 + d1;

#else

  if (taskwaypoint>0) {
    return DoubleDistance(task_stats[taskwaypoint-1].AATTargetLat,
			  task_stats[taskwaypoint-1].AATTargetLon,
			  latitude,
			  longitude,
			  task_stats[taskwaypoint+1].AATTargetLat,
			  task_stats[taskwaypoint+1].AATTargetLon);
  } else {
    double d1;
    DistanceBearing(latitude,
		    longitude,
		    task_stats[taskwaypoint+1].AATTargetLat,
		    task_stats[taskwaypoint+1].AATTargetLon,
		    &d1, NULL);
    return d1;
  }


#endif
}


void CalculateAATIsoLines(void) {
  double stepsize = 25.0;

  if(AATEnabled == FALSE)
    return;

  mutexTaskData.Lock();

  for(int i=1;i<MAXTASKPOINTS;i++) {

    if(ValidTaskPoint(i)) {
      if (!ValidTaskPoint(i+1)) {
        // This must be the final waypoint, so it's not an AAT OZ
        continue;
      }
      // JMWAAT: if locked, don't move it
      if (i<ActiveTaskPoint) {
        // only update targets for current/later waypoints
        continue;
      }

      int j;
      for (j=0; j<MAXISOLINES; j++) {
        task_stats[i].IsoLine_valid[j]= false;
      }

      double latitude = task_stats[i].AATTargetLat;
      double longitude = task_stats[i].AATTargetLon;
      double dist_0, dist_north, dist_east;
      bool in_sector = true;

      double max_distance, delta;
      if(task_points[i].AATType == SECTOR) {
        max_distance = task_points[i].AATSectorRadius;
      } else {
        max_distance = task_points[i].AATCircleRadius;
      }
      delta = max_distance*2.4 / (MAXISOLINES);
      bool left = false;

      /*
      double distance_glider=0;
      if ((i==ActiveTaskPoint) && (CALCULATED_INFO.IsInSector)) {
        distance_glider = DoubleLegDistance(i, GPS_INFO.Longitude, GPS_INFO.Latitude);
      }
      */

      // fill
      j=0;
      // insert start point
      task_stats[i].IsoLine_Latitude[j]= latitude;
      task_stats[i].IsoLine_Longitude[j]= longitude;
      task_stats[i].IsoLine_valid[j]= true;
      j++;

      do {
        dist_0 = DoubleLegDistance(i, longitude, latitude);

        double latitude_north, longitude_north;
        FindLatitudeLongitude(latitude, longitude,
                              0, stepsize,
                              &latitude_north,
                              &longitude_north);
        dist_north = DoubleLegDistance(i, longitude_north, latitude_north);

        double latitude_east, longitude_east;
        FindLatitudeLongitude(latitude, longitude,
                              90, stepsize,
                              &latitude_east,
                              &longitude_east);
        dist_east = DoubleLegDistance(i, longitude_east, latitude_east);

        double angle = AngleLimit360(RAD_TO_DEG*atan2(dist_east-dist_0, dist_north-dist_0)+90);
        if (left) {
          angle += 180;
        }

        FindLatitudeLongitude(latitude, longitude,
                              angle, delta,
                              &latitude,
                              &longitude);

        in_sector = InAATTurnSector(longitude, latitude, i);
        /*
        if (dist_0 < distance_glider) {
          in_sector = false;
        }
        */
        if (in_sector) {
          task_stats[i].IsoLine_Latitude[j] = latitude;
          task_stats[i].IsoLine_Longitude[j] = longitude;
          task_stats[i].IsoLine_valid[j] = true;
          j++;
        } else {
          j++;
          if (!left && (j<MAXISOLINES-2))  {
            left = true;
            latitude = task_stats[i].AATTargetLat;
            longitude = task_stats[i].AATTargetLon;
            in_sector = true; // cheat to prevent early exit

            // insert start point (again)
            task_stats[i].IsoLine_Latitude[j] = latitude;
            task_stats[i].IsoLine_Longitude[j] = longitude;
            task_stats[i].IsoLine_valid[j] = true;
            j++;
          }
        }
      } while (in_sector && (j<MAXISOLINES));

    }
  }
  mutexTaskData.Unlock();
}


void SaveDefaultTask(void) {
  mutexTaskData.Lock();
  if (!TaskAborted) {
    TCHAR buffer[MAX_PATH];
#ifdef GNAV
    LocalPath(buffer, TEXT("persist/Default.tsk"));
#else
    LocalPath(buffer, TEXT("Default.tsk"));
#endif
    SaveTask(buffer);
  }
  mutexTaskData.Unlock();
}

//////////////////////////////////////////////////////



bool TaskIsTemporary(void) {
  bool retval = false;
  mutexTaskData.Lock();
  if (TaskAborted) {
    retval = true;
  }
  if ((task_points[0].Index>=0) && (task_points[1].Index== -1)
      && (Task_saved[0] >= 0)) {
    retval = true;
  };

  mutexTaskData.Unlock();
  return retval;
}


static void BackupTask(void) {
  mutexTaskData.Lock();
  for (int i=0; i<=MAXTASKPOINTS; i++) {
    Task_saved[i]= task_points[i].Index;
  }
  active_waypoint_saved = ActiveTaskPoint;
  if (AATEnabled) {
    aat_enabled_saved = true;
  } else {
    aat_enabled_saved = false;
  }
  mutexTaskData.Unlock();
}


void ResumeAbortTask(const SETTINGS_COMPUTER &settings_computer, int set) {
  int i;
  int active_waypoint_on_entry;
  bool task_temporary_on_entry = TaskIsTemporary();

  mutexTaskData.Lock();
  active_waypoint_on_entry = ActiveTaskPoint;

  if (set == 0) {
    if (task_temporary_on_entry && !TaskAborted) {
      // no toggle required, we are resuming a temporary goto
    } else {
      TaskAborted = !TaskAborted;
    }
  } else if (set > 0)
    TaskAborted = true;
  else if (set < 0)
    TaskAborted = false;

  if (task_temporary_on_entry != TaskAborted) {
    if (TaskAborted) {

      // save current task in backup
      BackupTask();

      // force new waypoint to be the closest
      ActiveTaskPoint = -1;

      // force AAT off
      AATEnabled = false;

      // set MacCready
      if (!GlidePolar::AbortSafetyUseCurrent)  // 20060520:sgi added
	GlidePolar::SetMacCready(min(GlidePolar::GetMacCready(), 
				     GlidePolar::AbortSafetyMacCready()));

    } else {

      // reload backup task and clear it

      for (i=0; i<=MAXTASKPOINTS; i++) {
        task_points[i].Index = Task_saved[i];
	Task_saved[i] = -1;
      }
      ActiveTaskPoint = active_waypoint_saved;
      AATEnabled = aat_enabled_saved;

      RefreshTask(settings_computer);
    }
  }

  if (active_waypoint_on_entry != ActiveTaskPoint){
    SelectedWaypoint = ActiveTaskPoint;
  }

  mutexTaskData.Unlock();
}



int getFinalWaypoint() {
  int i;
  i=max(-1,min(MAXTASKPOINTS,ActiveTaskPoint));
  if (TaskAborted) {
    return i;
  }

  i++;
  mutexTaskData.Lock();
  while((i<MAXTASKPOINTS) && (task_points[i].Index != -1))
    {
      i++;
    }
  mutexTaskData.Unlock();
  return i-1;
}

bool ActiveIsFinalWaypoint() {
  return (ActiveTaskPoint == getFinalWaypoint());
}


bool IsFinalWaypoint(void) {
  bool retval;
  mutexTaskData.Lock();
  if (ValidTask() && (task_points[ActiveTaskPoint+1].Index >= 0)) {
    retval = false;
  } else {
    retval = true;
  }
  mutexTaskData.Unlock();
  return retval;
}


bool InAATTurnSector(const double longitude, const double latitude,
                    const int the_turnpoint)
{
  double AircraftBearing;
  bool retval = false;

  if (!ValidTaskPoint(the_turnpoint)) {
    return false;
  }

  double distance;
  mutexTaskData.Lock();
  DistanceBearing(WayPointList[task_points[the_turnpoint].Index].Latitude,
                  WayPointList[task_points[the_turnpoint].Index].Longitude,
                  latitude,
                  longitude,
                  &distance, &AircraftBearing);

  if(task_points[the_turnpoint].AATType ==  CIRCLE) {
    if(distance < task_points[the_turnpoint].AATCircleRadius) {
      retval = true;
    }
  } else if(distance < task_points[the_turnpoint].AATSectorRadius) {
    if (AngleInRange(task_points[the_turnpoint].AATStartRadial,
                     task_points[the_turnpoint].AATFinishRadial,
                     AngleLimit360(AircraftBearing), true)) {
      retval = true;
    }
  }

  mutexTaskData.Unlock();
  return retval;
}


bool WaypointInTask(const int ind) {
  if (!WayPointList || (ind<0)) return false;
  return WayPointList[ind].InTask;
}


void CheckStartPointInTask(void) {
  mutexTaskData.Lock();
  if (task_points[0].Index != -1) {
    // ensure current start point is in task
    int index_last = 0;
    for (int i=MAXSTARTPOINTS-1; i>=0; i--) {
      if (task_start_points[i].Index == task_points[0].Index) {
	index_last = -1;
	break;
      }
      if ((task_start_points[i].Index>=0) && (index_last==0)) {
	index_last = i;
      }
    }
    if (index_last>=0) {
      if (task_start_points[index_last].Index>= 0) {
	index_last = min(MAXSTARTPOINTS-1,index_last+1);
      }
      // it wasn't, so make sure it's added now
      task_start_points[index_last].Index = task_points[0].Index;
      task_start_stats[index_last].Active = true;
    }
  }
  mutexTaskData.Unlock();
}


void ClearStartPoints()
{
  mutexTaskData.Lock();
  for (int i=0; i<MAXSTARTPOINTS; i++) {
    task_start_points[i].Index = -1;
    task_start_stats[i].Active = false;
  }
  task_start_points[0].Index = task_points[0].Index;
  task_start_stats[0].Active = true;
  mutexTaskData.Unlock();
}

void SetStartPoint(const int pointnum, const int waypointnum)
{
  if ((pointnum>=0) && (pointnum<MAXSTARTPOINTS)) {
    // TODO bug: don't add it if it's already present!
    mutexTaskData.Lock();
    task_start_points[pointnum].Index = waypointnum;
    task_start_stats[pointnum].Active = true;
    mutexTaskData.Unlock();
  }
}

