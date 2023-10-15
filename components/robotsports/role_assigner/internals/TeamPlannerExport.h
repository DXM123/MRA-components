/**
 *  @file
 *  @brief   Class for team planning
 *  @curator Jürge van Eijck
 */
#ifndef TEAM_PLANNER_EXPORT_H
#define TEAM_PLANNER_EXPORT_H 1

#include "MovingObject.h"
#include "PlannerOptions.h"
#include "GlobalPathPlanner.h"
#include "WmTypes.h"
#include <vector>
#include "FieldConfig.h"
#include <string>


namespace trs {
	std::string GetTeamPlannerSVGname(game_state_e gamestate, std::string suffix = "");
} // namespace

#endif /* TEAM_PLANNER_H */
