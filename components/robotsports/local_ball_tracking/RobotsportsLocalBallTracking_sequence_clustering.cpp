
// generated protobuf types from interface of this component
#include "RobotsportsLocalBallTracking_sequence_clustering.hpp"
#include "RobotsportsLocalBallTracking_preprocessing.hpp"
#include "sequence_clustering_ball_model.hpp"
#include "sequence_clustering_balldef.hpp"
#include "sequence_clustering_best_uid.hpp"

static sc_global_data pscgd;
static bool initialized = false;

static int fbuf_init(hypothesis* phyp)
{
        /* clear feature buf */
        memset(&(phyp->fbuf), 0, sizeof(featbuf_t));

        /* no valid features yet */
        phyp->nfbuf = 0;

        /* start position in buffer */
        phyp->fbuf_idx = 0;

        return BM_SUCCESS;
}

static int ma_init(hypothesis* phyp)
{
        int i;

        phyp->ma_first = 1;

        phyp->ma_idx = MA_N;
        if (phyp->ma_idx >= MA_N + 1) {
                phyp->ma_idx = phyp->ma_idx - (MA_N + 1);
        }

        /* initialize buffer */
        for (i = 0; i < MA_N + 1; i++) {
               phyp->ma_buf[i] = 0.01;
        }

        phyp->mavg = 0.01;

        return BM_SUCCESS;
}


static int init_hyp(hypothesis* p_phyp) {
    /* initialize hypotheses */
    for (int i = 0; i < MAXHYP; i++) {
        (p_phyp + i)->ball_detected = false;
        (p_phyp + i)->p = 1.0;
        ma_init(p_phyp + i);
        fbuf_init(p_phyp + i);
    }

    return BM_SUCCESS;
}

static int initialize_tracking(MRA::RobotsportsLocalBallTracking::ParamsType const &params) {
    /* initial number of hypotheses */
    pscgd.nhyp = 1;

    /* initialize hypotheses */
    init_hyp(pscgd.hyp);
    init_hyp(pscgd.hyp2);

    pscgd.new_uid = 0;

    pscgd.track_uid = INVALID_UID;

    return BM_SUCCESS;
}

void local_ball_tracking_calculate_ball_now(const MRA::RobotsportsLocalBallTracking::InputType &input,
                               const MRA::RobotsportsLocalBallTracking::ParamsType &params,
                               MRA::RobotsportsLocalBallTracking::StateType &state) {
}


void local_ball_tracking_sequence_clustering(   unsigned nrBallsThisTime,
                            const std::vector<ball_candidate_t>& ballData,
                            const MRA::RobotsportsLocalBallTracking::InputType &input,
                            const MRA::RobotsportsLocalBallTracking::ParamsType &params,
                            MRA::RobotsportsLocalBallTracking::OutputType &output,
                            MRA::RobotsportsLocalBallTracking::StateType &state,
                            unsigned max_num_balls)
{

    if (not initialized) {
        initialize_tracking(params);
        initialized = true;
    }

    // now run sc_bm code
    ball_estimate_t ball_estimate;
    int use_next_best_ball = 0; // if 1, then go to next best ball
    int ret = sequence_clustering_ball_model(&ball_estimate, ballData, input.ts(), use_next_best_ball, &pscgd, params, max_num_balls);
    if (ret == BM_SUCCESS) {
        // update ball position in world model since a successful step has been done
        // update previous ball
        output.mutable_ball_prev()->CopyFrom(state.ball());
        output.mutable_ball()->set_x(ball_estimate.xhat); // position X, replaced TPB on 20161210 from ball_estimate.x
        output.mutable_ball()->set_y(ball_estimate.yhat); // position Y, replaced TPB on 20161210 from ball_estimate.y
        output.mutable_ball()->set_z(ball_estimate.z); // position Z
        output.mutable_ball()->set_vx(ball_estimate.xdot); // velocity in X
        output.mutable_ball()->set_vy(ball_estimate.ydot); // velocity in Y
        output.mutable_ball()->set_vz(ball_estimate.zdot); // velocity in Z
        output.mutable_ball()->set_confidence(ball_estimate.hconf); // moving average confidence
        output.mutable_ball()->set_timestamp(ball_estimate.timestamp); // timestamp based off of liveseconds
        double age = (input.ts() - ball_estimate.timestamp);
        if (age < 0) {
            age = 0.0;
        }
        // decay confidence
        output.mutable_ball_now()->set_confidence(
                ball_estimate.hconf * pow(params.confidence_decay(), age)); // degrade confidence for extrapolation based on age
                // retire when ball observation is too old
        if (age > params.ball_time_to_forget()) {
            output.mutable_ball()->set_confidence(0.0);
        }

        // Calculate ball_now()
        // Check usage : Seems not in use outside ball_process
        // TODO timestamp is updated when copy from ball_new to ball
        // TODO ball_prev is not updated, it seems - stays at 0
        // calculate ball position at current time, based on position with timestamp ts and estimate of ball speed
        double timeLeap = input.ts() - output.ball().timestamp();
        // copy ball position to _now position
        output.mutable_ball_now()->CopyFrom(output.ball());
        if (timeLeap > 0) {
            // current time is actually larger than ball observation, so we can extrapolate for _now position
            output.mutable_ball_now()->set_x(output.ball().x() + timeLeap * output.ball().vx()); // position X extrapolated
            output.mutable_ball_now()->set_y(output.ball().y() + timeLeap * output.ball().vy()); // position X extrapolated
            output.mutable_ball_now()->set_z(output.ball().z() + timeLeap * output.ball().vz()); // position X extrapolated
            output.mutable_ball_now()->set_confidence(
                    output.ball().confidence() * pow(params.confidence_decay(), timeLeap)); // degrade confidence for extrapolation based on timeLeap
            output.mutable_ball_now()->set_timestamp(input.ts()); // timestamp for extrapolation is current time
        }
        state.mutable_ball()->CopyFrom(output.ball());
    }
}
