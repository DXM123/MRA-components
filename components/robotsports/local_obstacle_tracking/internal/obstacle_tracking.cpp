/**
 *  @file
 *  @brief   object tracking process
 *  @curator Ton Peijnenburg
 *  @desc    determine best ball out of ball observations from various sensors
 *           using code from Tech United Turtle2, written by Rene van de Molengraft
 *           a very opportunistic interface is chosen, that
 *           leaves much of the original data structure in place
 */

#include "../../local_obstacle_tracking/RobotsportsLocalObstacleTracking_datatypes.hpp"
#include "../../local_obstacle_tracking/internal/obstacle_tracking.hpp"

#include <unistd.h>
#include <string.h>
#include <cmath>

#include "../../local_obstacle_tracking/internal/objectdef.hpp"
#include "../../local_obstacle_tracking/internal/sequence_clustering_track_objects.hpp"
#include "logging.hpp"

static scw_global_data pscgd;
static double objects_detected[DIM*MAX_TURTLES*MAXNOBJ_LOCAL];
static double pobj[4*MAX_TURTLES*MAXNOBJ_LOCAL];
static double pobj_radius[MAX_TURTLES*MAXNOBJ_LOCAL];
static double pobj_birthdate[MAX_TURTLES*MAXNOBJ_LOCAL];
static double pobj_assoc_buffer[MAX_TURTLES*MAXNOBJ_LOCAL];
static double pobj_label[MAX_TURTLES*MAXNOBJ_LOCAL];


obstacle_tracking_t object_process;

static int processObjects(MRA::RobotsportsLocalObstacleTracking::ObstacleCandidate obstacle_candidate, double* objectData)
{
    /* copy object position */
    objectData[0] = obstacle_candidate.pose_fcs().x();			// x position in extrapolated coordinates
    objectData[1] = obstacle_candidate.pose_fcs().y();			// y position in extrapolated coordinates
    objectData[2] = obstacle_candidate.radius(); 			// radius of object
    objectData[3] = 0;							// label = 0 is for unknown (default)
    objectData[4] = google::protobuf::util::TimeUtil::TimestampToMilliseconds(obstacle_candidate.timestamp()) / 1000.0;				// timestamp

    return 1;
}

int obstacle_tracking_initialize(double timestamp)
{
    pos_t    zero_pos    = { 0.0, 0.0, 0.0, 0.0 };
    object_t zero_object = { 0, zero_pos, zero_pos, 0.0, 0.0, 0.0 };

    // initialize object memory in output part of the sensor
    for (int i = 0; i < MAXNOBJ_GLOBAL; i++) {
    	object_process.out[i]= zero_object;
    }

    object_process.nobj = 0;
    object_process.last_processed_vision_ts =  -1.0;
    object_process.update_interval_vision = 0.05;
    object_process.last_processed_selves_ts = -1.0;
    object_process.update_interval_selves = 0.05;

    // initialize static data structure for administration of objects by/from filter
    for(int i=0;i<MAXNOBJ_GLOBAL;i++){
        pobj[4*i]		= 0.0;
        pobj[4*i+1]		= 0.0;
        pobj[4*i+2]		= 0.0;
        pobj[4*i+3]		= 0.0;
        pobj_radius[i]	= 0.0;
        pobj_label[i]	= 0.0;
        pobj_birthdate[i]		= 1e10;		// put birth date in the future
        pobj_assoc_buffer[i]	= 0.0;
    }

    // default minimum confidence level for shared selves
    object_process.use_shared_selves = 1;
    object_process.min_conf_shared_selves = 0.5;

    // initialize parameters
    // parameters for Turtle2 code
    object_process.par_pclutter = 0.01;	/* probability that new measurement is clutter, default value is 0.001, changed to 0.02 after experimentation on 20170404 */
    object_process.par_alpha = 0.01;	/* pnew = alpha * pexist, default value is 0.01 */
    object_process.par_sigmax = 0.25;	/* standard deviation measurement noise, x-direction, default value is 0.250 */
    object_process.par_sigmay = 0.25;	/* standard deviation measurement noise, y-direction, default value is 0.250 */
    object_process.par_nkeep = 20;		/* number of hypotheses saved for next step, default is 30 */
    object_process.par_pfactor = 10.0;	/* reject hypotheses with p < pmax/pfactor, default is 100.0 */
    object_process.par_nselect = 32;		/* number of selected obstacles from input list, default is 385 */
    object_process.par_maxnobj = 10;		/* maximum number of objects in an hypothesis, default is 20 */
    object_process.par_clipradius = 1000.0;/* clipping radius relative to own position, default is 1000.0 */
    object_process.par_kscale = 0.5;	/* scale factor for Kalman gain, default is 0.9 */
    object_process.par_maxage = 1.0;	/* maximum age for non-updated objects, was 0.5 */
    object_process.par_labelbound = 0.60;	/* minimum required likelihood for object association, default value is 0.95 */

    object_process.filter_error =  0;		// this parameter is set if an error in the filter occurs, e.g. out of Kalman filters
    object_process.reset_on_error =  1;

    // initialize data structure for sequential clustering with first hypothesis
    return init_sc_wm(pscgd, timestamp);
}

void obstacle_tracking(double timestamp,
                        MRA::RobotsportsLocalObstacleTracking::Input const& input,
                        MRA::RobotsportsLocalObstacleTracking::Params const &params,
                        MRA::RobotsportsLocalObstacleTracking::State &state,
                        MRA::RobotsportsLocalObstacleTracking::Output &output)
{
    if (not state.is_initialized()) {
        obstacle_tracking_initialize(timestamp);
        state.set_is_initialized(true);
    }

    // fill measurements just like balltrackpreproc.c does in Turtle2 code
	// only now we fill with obstacles
    /* check for each sensor if new data is received */

    pos_t		zero_pos    = { 0.0, 0.0, 0.0, 0.0 };
    object_t	zero_object = { 0, zero_pos, zero_pos, 0.0, 0.0, 0.0 };

    // reset filter if error has occurred and reset_on_error is set to true
    if (object_process.filter_error and object_process.reset_on_error) {
    	object_process.filter_error = 0;
    	object_process.filter_reset_time = timestamp;
    	init_sc_wm(pscgd, timestamp);
    }

    // copy filter parameters from shared memory so they can be updated run-time (not compile-time)
    pscgd.par.pclutter	= object_process.par_pclutter;	// tunable parameter, default value is 0.001
    pscgd.par.alpha		= object_process.par_alpha;		// tunable parameter, default value is 0.01
    pscgd.par.sigmax	= object_process.par_sigmax;		// tunable parameter, default value is 0.250
    pscgd.par.sigmay	= object_process.par_sigmay;		// tunable parameter, default value is 0.250
    pscgd.par.nkeep		= (int) object_process.par_nkeep;		// tunable parameter, default value is 30
    pscgd.par.pfactor	= object_process.par_pfactor;		// tunable parameter, default value is 100.0
    pscgd.par.nselect	= (int)object_process.par_nselect;		// tunable parameter, default value is 385
    pscgd.par.maxnobj	= (int)object_process.par_maxnobj;		// tunable parameter, default value is 20
    if (pscgd.par.maxnobj > MAXNOBJ_GLOBAL) {
        pscgd.par.maxnobj = MAXNOBJ_GLOBAL;
    }
    pscgd.par.clipradius = object_process.par_clipradius;	// tunable parameter, default value is 1000.0
    pscgd.par.kscale	= object_process.par_kscale;		// tunable parameter, default value is 0.9
    pscgd.par.maxage	= object_process.par_maxage;		// tunable parameter, default value is 0.3
    pscgd.par.labelbound = object_process.par_labelbound;	// default value is 0.95

    double last_processed_vision_timestamp = object_process.last_processed_vision_ts;
    double last_processed_selves_timestamp = object_process.last_processed_selves_ts;

    // first sensor is omni_vision
    // check for updated object positions first, only then update object positions in extrapolated coordinates
    // TODO may be replaced by just single check of vision updates master ts field hw.omni.ts with latest timestamp
    // check if input data is newer than data stored during the last sample

    // initialize obstacle counter
    unsigned obstacles_this_time = 0;

    int nr_obstacles = input.obstacle_candidates_size();
    MRA_LOG_DEBUG("INPUT nr_obstacles =  %d", nr_obstacles);


    // start with omni vision reports
    // process when > 0 obstacles have been reported
    if (nr_obstacles > 0) {
    	// include limit on nr_obstacles? does not seem to be required; nr_obstacles should always be less or equal to maximum amount of reported obstacles
        // get time stamp for new information
        double last_omni_timestamp = -1.0;
        for (int i = 0; i < nr_obstacles; i++) {
            double obstacle_ts = google::protobuf::util::TimeUtil::TimestampToMilliseconds(input.obstacle_candidates(i).timestamp()) / 1000.0;
            MRA_LOG_DEBUG("obstacle [%d] ts: %f", i, obstacle_ts);
            if (obstacle_ts > last_omni_timestamp) {
                last_omni_timestamp = obstacle_ts;
            }
        }
        MRA_LOG_DEBUG("last_omni_timestamp: %f", last_omni_timestamp);
        // check for new features from omni camera - a sinle new obstacle is enough!
        //printf("object_process: objects %d last_omni %f last_object_process %f\n", nr_obstacles, last_omni_timestamp, last_processed_vision_timestamp);
        MRA_LOG_DEBUG("last_processed_vision_timestamp: %f", last_processed_vision_timestamp);
        if (last_omni_timestamp > last_processed_vision_timestamp) {
            // we have new features!
            // copy to Tech United data structure called objects_detected array
            // first, copy objects found by omni_vision
            for (auto i = 0; i < nr_obstacles; i++) {
                // processObjects will move the data to the Tech United data structure
            	// add only observations that are newer than (last update + interval)
                double obstacle_ts = google::protobuf::util::TimeUtil::TimestampToMilliseconds(input.obstacle_candidates(i).timestamp()) / 1000.0;
            	if (obstacle_ts > last_processed_vision_timestamp) {
					processObjects(input.obstacle_candidates(i), &objects_detected[obstacles_this_time*DIM]);
					MRA_LOG_DEBUG("process_objects has added obstacle at (%f,%f) with label %d and timestamp %f\n",
					            objects_detected[obstacles_this_time*DIM],
					            objects_detected[obstacles_this_time*DIM+1],
					            objects_detected[obstacles_this_time*DIM+3],
					            objects_detected[obstacles_this_time*DIM+4]);
					obstacles_this_time++;
					// only update last_processed_vision_ts if at least one omni object has been used for updating the filter
		            object_process.last_processed_vision_ts = last_omni_timestamp;
            	}
            }
        }
        MRA_LOG_DEBUG("object_process.last_processed_vision_ts: %f", object_process.last_processed_vision_ts);
    }

    // add communicated positions of team-mates, if available
    if (object_process.use_shared_selves) {
    	double last_shared_selves_timestamp = 0.0;
        // check against last updated timestamp for shared selves information
        for (int m = 0; m < input.communicated_teammates_size(); m++) {
            auto player_ts = google::protobuf::util::TimeUtil::TimestampToMilliseconds(input.communicated_teammates(m).timestamp()) / 1000.0;
            if (player_ts > last_shared_selves_timestamp) {
                last_shared_selves_timestamp = player_ts;
            }

        }
        // last_processed_selves_timestamp was previously initialized
        if (last_shared_selves_timestamp > last_processed_selves_timestamp) {
			for (int j = 0; j < input.communicated_teammates_size(); j++) {
				// only pick if player has sufficient confidence and time stamp more recent than (last update + interval)
	            auto player_ts = google::protobuf::util::TimeUtil::TimestampToMilliseconds(input.communicated_teammates(j).timestamp()) / 1000.0;
				if ((input.communicated_teammates(j).confidence() > object_process.min_conf_shared_selves)
				    and (player_ts > (last_processed_selves_timestamp + object_process.update_interval_selves))) {
					// we have a valid shared self
					if (obstacles_this_time < MAX_TURTLES*MAXNOBJ_LOCAL) {
						// add it if still room
						processObjects(input.communicated_teammates(j), &objects_detected[obstacles_this_time*DIM]);
						objects_detected[obstacles_this_time*DIM+3] = (j+1); // label field replaced by robot ID (array index + 1)
						objects_detected[obstacles_this_time*DIM+4] = timestamp; // TODO remove when proper timestamp is communicated
						//printf("process_objects has added player   at (%f,%f) with label %f and timestamp %f\n", objects_detected[obstacles_this_time*DIM], objects_detected[obstacles_this_time*DIM+1], objects_detected[obstacles_this_time*DIM+3], objects_detected[obstacles_this_time*DIM+4]);
						obstacles_this_time++;
						// only update last_processed_selves_ts if at least one shared self has been used for updating the filter
						object_process.last_processed_selves_ts = last_shared_selves_timestamp;
					}
				}
			}
        }
    }

    if (obstacles_this_time > MAX_TURTLES*MAXNOBJ_LOCAL) {
        MRA_LOG_DEBUG("warning: object_process has more input objects this time than can be handled");
	}
    MRA_LOG_DEBUG("object_process objects than can be handled: %d", obstacles_this_time);

    // stub with zero objects;
    auto zero_obstacle = MRA::RobotsportsLocalObstacleTracking::ObstacleCandidate();
    for (int k = obstacles_this_time; k < MAX_TURTLES*MAXNOBJ_LOCAL; k++) {
		processObjects(zero_obstacle, &objects_detected[k*DIM]);
    }

    // update administration
    object_process.nr_objects = obstacles_this_time;

    // now run sc_bm code if obstacles are found
    unsigned pnobj;
    int ret = sc_wm(timestamp, pobj, pobj_radius, pobj_birthdate, pobj_assoc_buffer, pobj_label, pnobj, objects_detected, obstacles_this_time,  pscgd);
    MRA_LOG_DEBUG("object process | time: %6.3f  return: %3d  nr_obstacles: %3d obstacles: %3d pnob: %3d", timestamp, ret, nr_obstacles, obstacles_this_time, pnobj);
    if (ret == SC_SUCCESS) {
        // update object positions in world model since a successful step has been done
        // copy position and velocity data, and size, for all objects reported by filter
        //    	printf("#objects clustered %d\n", pnobj);
        unsigned i = 0;
        output.mutable_objects()->Clear();
        for (i = 0; i < pnobj; i++) {
            object_process.out[i].pos.x = pobj[4*i];
            object_process.out[i].vel.x = pobj[4*i+1];
            object_process.out[i].pos.y = pobj[4*i+2];
            object_process.out[i].vel.y = pobj[4*i+3];
            object_process.out[i].size =  pobj_radius[i];
            object_process.out[i].ts = timestamp;
            if (pobj_label[i] < OBJECT_LABEL_OWNTEAM) {
                object_process.out[i].type = OBJECT_PLAYER_US;
            }
            else {
                object_process.out[i].type = OBJECT_PLAYER_THEM;
            }
            object_process.out[i].confidence = 0.8;
            // TODO temporary assignment of object filter label to col.r for debugging purposes
            //      may be used for association of opponent robot detections - this is reported but may be difficult due to noise
            object_process.out[i].col.r = pobj_label[i];

            auto trackedObject = MRA::Datatypes::TrackedObject();
            trackedObject.mutable_pos_vel_fcs()->mutable_position()->set_x(object_process.out[i].pos.x);;
            trackedObject.mutable_pos_vel_fcs()->mutable_velocity()->set_x(object_process.out[i].vel.x);
            trackedObject.mutable_pos_vel_fcs()->mutable_position()->set_y(object_process.out[i].pos.y);;
            trackedObject.mutable_pos_vel_fcs()->mutable_velocity()->set_y(object_process.out[i].vel.y);
            auto timestamp_obj = google::protobuf::util::TimeUtil::MillisecondsToTimestamp(object_process.out[i].ts * 1000);
            trackedObject.mutable_timestamp()->CopyFrom(timestamp_obj);
            trackedObject.set_confidence(object_process.out[i].confidence);
            trackedObject.set_type(object_process.out[i].type);
            output.mutable_objects()->Add()->CopyFrom(trackedObject);
        }
        // stub remaining out fields with zero objects
        while (i < MAXNOBJ_GLOBAL) {
            object_process.out[i] = zero_object;
            i++;
        }
        // include number of (valid) objects
        object_process.nobj = (long)pnobj;
        // include time stamp
        object_process.ts = timestamp;
    } // if return is successful, otherwise no update

}
