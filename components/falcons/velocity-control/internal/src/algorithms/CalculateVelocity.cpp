/*
 * CalculateVelocity.cpp
 *
 *  Created on: July, 2019
 *      Author: Jan Feitsma
 */

#include "VelocityControlAlgorithms.hpp"


CalculateVelocity::CalculateVelocity(boost::function<AbstractVelocitySetpointController *(void)> callback)
{
    _callback = callback;
}

void CalculateVelocity::execute(VelocityControlData &data)
{

    // pathPlanning library should ensure some controller is configured
    // we trigger this just-in-time operation via a callback, which makes it possible
    // for instance to overrule runtime whichever controller with Tokyo drift from associated algorithm
    auto controller = _callback();
    assert(controller != NULL);

    // call, check result, store output
    Velocity2D resultVelocity;
    bool success = controller->calculate(data, resultVelocity);
    if (!success)
    {
        resultVelocity = Velocity2D(0.0, 0.0, 0.0);
    }

    if (data.robotPosVelMoveType == robotPosVelEnum::POSVEL || data.robotPosVelMoveType == robotPosVelEnum::POS_ONLY)
    {
        // if needed, transform from FCS to RCS
        if (data.vcSetpointConfig.coordinateSystem == CoordinateSystemEnum::RCS)
        {
            // already in the correct coordinate system
            data.resultVelocityRcs = resultVelocity;
        }
        else
        {
            // need a transformation
            data.resultVelocityRcs = resultVelocity.transform_fcs2rcs(data.currentPositionFcs);
        }
    }
    else if (data.robotPosVelMoveType == robotPosVelEnum::VEL_ONLY)
    {
        data.resultVelocityRcs = resultVelocity;
    }

    // store for next iteration
    data.previousTimestamp = data.timestamp;
    data.previousVelocityRcs = data.resultVelocityRcs;

}

