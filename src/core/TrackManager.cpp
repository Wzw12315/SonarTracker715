#include "TrackManager.h"
#include <cmath>
#include <limits>
#include <algorithm>

TrackManager::TrackManager() : internal_id_counter(0), confirmed_target_count(0) {
    // 基础配置
    ASSOCIATION_GATE = 6.0;  // 关联波门阈值
    M_HITS = 3;             // 【对齐 MATLAB】: 从 2 提升至 3，要求连续命中 3 帧才能转正
}

QList<TargetTrack> TrackManager::updateTracks(const std::vector<double>& detected_angles,
                                              const std::vector<int>& detected_locs,
                                              const std::vector<double>& angles_cbf_all)
{
    int num_detected = detected_angles.size();
    std::vector<bool> detected_unassigned(num_detected, true);
    std::vector<bool> track_updated(m_tracks.size(), false);

    // 1. 最近邻关联匹配
    for (int t = 0; t < m_tracks.size(); ++t) {
        double last_ang = m_tracks[t].currentAngle;
        double min_dist = std::numeric_limits<double>::max();
        int min_idx = -1;

        for (int d = 0; d < num_detected; ++d) {
            double dist = std::abs(detected_angles[d] - last_ang);
            if (dist < min_dist) { min_dist = dist; min_idx = d; }
        }

        if (min_idx != -1 && min_dist <= ASSOCIATION_GATE && detected_unassigned[min_idx]) {
            m_tracks[t].currentAngle = detected_angles[min_idx];
            m_tracks[t].currentLoc = detected_locs[min_idx];
            m_tracks[t].isActive = true;
            m_tracks[t].missedCount = 0;
            m_tracks[t].age++;
            m_tracks[t].totalHits++;

            // 【核心转正逻辑】：对齐 MATLAB 严格命中计数
            if (!m_tracks[t].isConfirmed && m_tracks[t].totalHits >= M_HITS) {
                m_tracks[t].isConfirmed = true;
                m_tracks[t].id = ++confirmed_target_count;
            }

            // 匹配 CBF 方位
            double min_cbf_dist = std::numeric_limits<double>::max();
            double best_cbf = detected_angles[min_idx];
            for(double cbf_a : angles_cbf_all) {
                if (std::abs(cbf_a - detected_angles[min_idx]) < min_cbf_dist) {
                    min_cbf_dist = std::abs(cbf_a - detected_angles[min_idx]);
                    best_cbf = cbf_a;
                }
            }
            m_tracks[t].currentAngleCbf = best_cbf;

            detected_unassigned[min_idx] = false;
            track_updated[t] = true;
        }
    }

    // 2. 状态更新与丢弃逻辑
    for (int t = 0; t < m_tracks.size(); ++t) {
        if (!track_updated[t]) {
            m_tracks[t].age++;
            m_tracks[t].missedCount++;
            m_tracks[t].isActive = false;
        }
    }

    // 【核心修剪 - 对齐 MATLAB 幽灵修剪】：
    // 如果一个目标尚未转正，且丢失超过 1 帧，立刻抹杀，杜绝偶然噪点建轨
    for (int t = m_tracks.size() - 1; t >= 0; --t) {
        if (!m_tracks[t].isConfirmed && m_tracks[t].missedCount >= 1) {
            m_tracks.removeAt(t);
        }
    }

    // 3. 创建新试探航迹
    for (int d = 0; d < num_detected; ++d) {
        if (detected_unassigned[d]) {
            TargetTrack new_track;
            new_track.internal_id = ++internal_id_counter;
            new_track.id = -1;
            new_track.isConfirmed = false;
            new_track.totalHits = 1;
            new_track.age = 1;
            new_track.isActive = true;
            new_track.missedCount = 0;
            new_track.currentAngle = detected_angles[d];
            new_track.currentLoc = detected_locs[d];

            double min_cbf_dist = std::numeric_limits<double>::max();
            double best_cbf = detected_angles[d];
            for(double cbf_a : angles_cbf_all) {
                if (std::abs(cbf_a - detected_angles[d]) < min_cbf_dist) {
                    min_cbf_dist = std::abs(cbf_a - detected_angles[d]);
                    best_cbf = cbf_a;
                }
            }
            new_track.currentAngleCbf = best_cbf;
            m_tracks.append(new_track);
        }
    }

    return m_tracks;
}
