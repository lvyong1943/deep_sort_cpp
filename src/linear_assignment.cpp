#include "linear_assignment.h"
#include "hungarian_alg.h"
#include <iostream>

void min_cost_matching(Metric distance_metric, float max_distance, 
        vector<Track> tracks, vector<Detection> detections, 
        vector<Match>* matches, vector<int>* unmatched_tracks,
        vector<int>* unmatched_detections, vector<int> track_indices,
        vector<int> detection_indices)
{
    /*
     * solve linear assignment peoblem
     * Parameters
     * -------------
     *  distance_metric: callable Eigen::MatrixXf (vector<Track>, vector<Detection>
     *      vector<int>, vector<int>
     *      the distance metric is given a list of tracks and detections as well
     *      as a list of N track indices and M detection indices. the metric
     *      should return N*M dimensional cost_matrix
     *  max_distance: float
     *      gating threshold. associations with cost larger than this value are
     *      disregarded
     *  tracks: list[Tracks]
     *  detections: list[Detection]
     *  track_indices: list[int]
     *      the indices of tracks used for calculating cost_matrix, default is 
     *      [], which means all of the tracks
     *  detection_indices: list[int]
     *      the indices of detections used for calculating cost_matrix, default is 
     *      [], which means all of the detections
     *
     * returns
     * ---------
     *  matches: vector<Match>
     *      matched indices of tracks and detections
     *  unmatched_tracks: vector<int>
     *      list of unmatched track indices
     *  unmatched_detections: vector<int>
     *      list of unmatched detection indices
     */
    // cout << "enter min_cost_matching......" << endl;
    if(track_indices.size()<1 || detection_indices.size()<1)
    {
        *unmatched_tracks = track_indices;
        *unmatched_detections = detection_indices;
        return;
    }

    Eigen::MatrixXf cost_matrix;
    cost_matrix = distance_metric(tracks, detections, track_indices, detection_indices);
    cost_matrix = cost_matrix.array().min(max_distance+1e-5);


    // use hungarian alg solve linear assignment problem
    AssignmentProblemSolver aps;
    size_t N = cost_matrix.rows();
    size_t M = cost_matrix.cols();
    Eigen::MatrixXf cost_vector = cost_matrix;
    cost_vector.resize(1,N*M);
    const vector<float> cost_matrix_vec(cost_vector.data(), cost_vector.data()+N*M);
    vector<int> assignment(N);
    aps.Solve(cost_matrix_vec, N, M, assignment, AssignmentProblemSolver::optimal);
    
    // clear all  return matrix
    matches->clear(); 
    unmatched_detections->clear();
    unmatched_tracks->clear();
    // process detections not in matches
    for(size_t i = 0; i < detection_indices.size(); i++)
    {
        // vector<int>::iterator it;
        if(find(assignment.begin(), assignment.end(), i) == assignment.end())
            unmatched_detections->push_back(detection_indices[i]);
    }

    size_t zero_count = count(assignment.begin(), assignment.end(), 0);
    if(zero_count>1)
    {
        vector<int>::iterator it = find(assignment.begin(), assignment.end(), 0);
        for(size_t i = 1; i < zero_count; ++i)
        {
            it = find(it+1, assignment.end(), 0);
            *it = -1;
        }
    }


    // process matches and tracks not in matches and matches that has large cost
    for(int row = 0; row < assignment.size(); row++)
    {
        int col = assignment[row];
        // cout << "assignment: (" << row << "," << col << ")" << endl;
        if(col == -1)
            unmatched_tracks->push_back(track_indices[row]);
        else{
            int track_idx = track_indices[row];
            int detection_idx = detection_indices[col];
            if(cost_matrix(row, col) > max_distance)
            {
                unmatched_tracks->push_back(track_idx);
                unmatched_detections->push_back(detection_idx);
            }
            else
            {
                Match match= {track_idx, detection_idx};
                matches->push_back(match);
            }
        }
    }
}

void matching_cascade(Metric distance_metric, float max_distance, 
        int cascade_depth, 
        vector<Track> tracks, vector<Detection> detections, 
        vector<Match>* matches, vector<int>* unmatched_tracks, 
        vector<int>* unmatched_detections, vector<int> track_indices, 
        vector<int> detection_indices)
{
    /*
     * matching existed tracks and current detections
     * Parameter
     * -------
     *  distance_metric: callable function
     *      distance metric is given a list of tracks and detections, as well as
     *      a list of N tracks indices and M detection indices. The metric should
     *      return N*M dimensional cost matrix, where element (i, j) is the
     *      association cost between i-th track in the given track indices and 
     *      j-th detection in the given detection indices
     *
     *  max_distance: float
     *      gating threshold, associations with cost larger than this value are 
     *      disregarded
     *
     *  cascade_depth: int
     *      cascade depth should be see to the maximum track age
     *  tracks: vector<Track>
     *      list of predicted tracks at the current time step
     *  detections: vector<Detection>
     *      list of detections at the current time step
     *  track_indices: Optional  vector<int>
     *      list of track indices. maps to row of cost matrix, default to all tracks
     *  detection_indices: Optional vectot<int>
     *      list of detection indices. maps to column of cost matrix, default to all detections
     *
     * Returns
     * --------
     *  matched:vector<tuple>
     *      matched track and detection indices 
     *  unmatched_tracks: vector<int>
     *      umatched track indices
     *  unmatched_detections: vector<int>
     *      unmatched detection indices
     */


    // cout << "enter matching_cascade....." << endl;
    *unmatched_detections = detection_indices;
    for(int level = 0; level < cascade_depth; level ++)
    {
        if(unmatched_detections->size() < 1) //no detection left
            break;

        vector<int> track_indices_l;
        for(vector<int>::iterator it = track_indices.begin(); 
                it != track_indices.end(); it ++)
            if(tracks[*it].time_since_update_ == 1+level)
                track_indices_l.push_back(*it);
        if(track_indices_l.size() < 1) //nothing to match ath this level
            continue;

        vector<int> unmatched_tracks_l;
        vector<Match> matches_l;
        min_cost_matching(distance_metric, max_distance, tracks, detections,
                &matches_l, &unmatched_tracks_l, unmatched_detections, 
                track_indices_l , *unmatched_detections);

        for(vector<Match>::iterator it=matches_l.begin(); it != matches_l.end(); it ++)
        {
            matches->push_back(*it);
            // delete the matched tracks in track_indices
            vector<int>::iterator iit;
            iit = find(track_indices.begin(), track_indices.end(), it->track_idx);
            if(iit != track_indices.end())
                track_indices.erase(iit);
        }
    }
    *unmatched_tracks = track_indices;

}


Eigen::MatrixXf gate_cost_matrix(KalmanFilter kf, Eigen::MatrixXf cost_matrix, 
        vector<Track> tracks, vector<Detection> detections, 
        vector<int> track_indices, vector<int> detection_indices, 
        float gated_cost, bool only_position)
{
    /*
     * invalidate infeasible entries in cost matrix based on the state distribution
     * obtained by kalman filter
     *
     * Parameters:
     * ----------
     *  kf: KalmanFilter
     *  cost_matrix: MatrixXf
     *      the N*M dimensional cost matrix, where N is the bumber of track indices
     *      and M is the number of detection indices
     *  tracks: vector<Track>
     *      vector of Track at current time step
     *  detections: vector<Detection>
     *      vector of detections at current time step
     *  track_indices: vector<int>
     *      vector of track indices that maps rows in 'cost_matrix' to tracks in
     *      'tracks'
     *  detection_indices: vector<int>
     *      vector of detection indices that maps cols in 'cost_matrix' to detections
     *      int 'detections'
     *  gated_cost: Optional[float]
     *      Entries in the cost matrix corresponding to indeasible association are
     *      set this value. defaults to a very large value
     *  only_position: Optional[bool]
     *      if true, only x, y of state distribution is considered, Default to false
     *
     *  Returns:
     *  -------
     *  cost_matrix:
     *      modified cost matrix
     */

    int gating_dim = 4;
    if(only_position)
        gating_dim = 2;

    float gating_threshold = chi2inv95[gating_dim];

    Eigen::MatrixXf measurements;
    measurements.resize(detection_indices.size(), 4);
    for(size_t i = 0; i < detection_indices.size(); ++i)
    {
        vector<float> tmp = detections[detection_indices[i]].to_xyah();
        measurements.row(i) = Eigen::VectorXf::Map(&tmp[0], tmp.size());
    }

    Eigen::VectorXf gating_distance_;
    for(size_t i = 0; i < track_indices.size(); ++i)
    {
        Track track = tracks[track_indices[i]];
        gating_distance_ = kf.gating_distance(track.mean_, track.cov_, measurements, only_position);

        for(size_t j = 0; j < gating_distance_.size(); j++)
            if(gating_distance_(j) > gating_threshold) 
                cost_matrix(i, j) = gated_cost;
    }
    return cost_matrix;
}



// int main(int argc, char** argv)
// {
//     Eigen::MatrixXf b;
//     b.resize(8,4);
//     b <<
//         1,2,3,4,
//         5,4,3,2,
//         3,7,9,12,
//         7,8,9,1,
//         12,4,2,11,
//         3,5,8,22,
//         5,6,1,3,
//         44,5,7,9;
//     // b = b.array().min(10.1);
//     cout << b <<endl;
// 
//     AssignmentProblemSolver aps;
//     Eigen::MatrixXf b_evec = b;
//     b_evec.resize(1,32);
//     const vector<float> b_vec(b_evec.data(), b_evec.data()+b_evec.rows()*b_evec.cols());
//     vector<int> assignment;
//     aps.Solve(b_vec, b.rows(), b.cols(), assignment, AssignmentProblemSolver::optimal);
//     for(vector<int>::iterator it=assignment.begin(); it != assignment.end(); it++)
//         cout << *it << endl;
// }
//     vector<vector<int> > a={{1,2,3,4},{1,2,3,5}};
//     vector<int> c = {1,2,3,3};
//     a.push_back(c);
//     for(size_t i = 0; i< a.size(); i++)
//     {
//         for(size_t j = 0; j < a[0].size(); j++)
//             cout << a[i][j] << ",";
//         cout <<endl;
//     }
// 
//     // for(vector<vector<float> >::iterator it=b.begin(); it != b.end(); it++)
//     // {
//     //     for(vector<float>::iterator iit=it->begin(); iit != it->end(); iit++)
//     //         cout << *iit ;
//     //     cout << endl;
//     //  }
//     // vector<int> track_indices = {1,2,3,4,5,6,7,8,9,0,10};
//     // vector<vector<int> > matches = {{1,2},{3,4},{5,6},{7,8},{9,0}};
//     // for(vector<vector<int> >::iterator it=matches.begin(); it != matches.end(); it ++)
//     // {
//     //     // delete the matched tracks in track_indices
//     //     vector<int>::iterator iit;
//     //     iit = find(track_indices.begin(), track_indices.end(), (*it)[0]);
//     //     if(iit != track_indices.end())
//     //         track_indices.erase(iit);
//     // }
//     // for(vector<int>::iterator it=track_indices.begin(); it != track_indices.end(); it++)
//     //     cout << *it << endl;
// }
