/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2026 by İlyas Akın for the macOS/Linux port: moved here from GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp; see NOTICE.md and the git history.

// FILE: WaterGridMotion.cpp //////////////////////////////////////////////////////////////////////
// Desc:   The water grid's mesh motion, moved out of WaterRenderObjClass::update (T1c, defect 17).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* The body is W3DWater.cpp's text, with only the substitutions WaterGridMotion.h lists. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "GameLogic/WaterGridMotion.h"

// ------------------------------------------------------------------------------------------------
void WaterGridMotion::updateForLogicFrame( UnsignedInt currLogicFrame, Bool doWaterGrid, Bool &meshInMotion,
	MeshPoint *meshData, Int gridCellsX, Int gridCellsY )
{
	static UnsignedInt lastLogicFrame = 0;
	if( lastLogicFrame != currLogicFrame )
	{
		// for vertex animated water we need to update the vector field
		if( doWaterGrid && meshInMotion == TRUE )
		{
			const Real PREFERRED_HEIGHT_FUDGE = 1.0f;		///< this is close enough to at rest
			const Real AT_REST_VELOCITY_FUDGE = 1.0f;		///< when we're close enought to at rest height and velocity we will stop
			const Real WATER_DAMPENING = 0.93f;					///< use with up force of 15.0
			Int i, j;
			Int	mx = gridCellsX+1;
			Int my = gridCellsY+1;
			MeshPoint *pData;

			//
			// we will mark the mesh as clean now ... if any of the fields are still in motion
			// they will continue to mark the mesh as dirty so processing continues next frame
			//
			meshInMotion = FALSE;

			// go through each mesh point and adjust the height according to the velocity
			for( j = 0, pData = meshData; j < (my + 2); j++ )
			{	

				for( i = 0; i < (mx + 2); i++ )
				{

					// only pay attention to mesh points that are in motion
					if( BitTest( pData->status, IN_MOTION ) )
					{

						// DAMPENING to slow the changes down
						pData->velocity *= WATER_DAMPENING;

						// if the height here is below our preferred height, we want to add upward force to counteract it
						if( pData->height < pData->preferredHeight )
							pData->velocity -= TheGlobalData->m_gravity * 3.0f;
						else				
							pData->velocity += TheGlobalData->m_gravity * 3.0f;

						// adjust the height at this grid location according to the current velocity		
						pData->height = pData->height + pData->velocity;

						//
						// if we are close enough to our preferred height and our velocity is small enough
						// this will be our resting location
						//
						if( fabs( pData->height - pData->preferredHeight ) < PREFERRED_HEIGHT_FUDGE &&
								fabs( pData->velocity ) < AT_REST_VELOCITY_FUDGE )
						{

							BitClear( pData->status, IN_MOTION );
							pData->height = pData->preferredHeight;
							pData->velocity = 0.0f;

						}  // end if
						else
						{

							// there is still motion in the mesh, we need to process next frame
							meshInMotion = TRUE;

						}  // end else

					}  // end if

					// on to the next one
					pData++;

				}  // end for i

			}  // end for j

		}  // end if

		// mark the last logic frame we processed on
		lastLogicFrame = currLogicFrame;

	}  // end if, a logic frame has passed

}  // end updateForLogicFrame
