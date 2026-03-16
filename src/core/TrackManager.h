#pragma once
#include <QList>
#include <vector>
#include "DataTypes.h"

class TrackManager {
public:
    TrackManager();

    QList<TargetTrack> updateTracks(const std::vector<double>& detected_angles,
                                    const std::vector<int>& detected_locs,
                                    const std::vector<double>& angles_cbf_all);

    int getConfirmedTargetCount() const { return confirmed_target_count; }

private:
    double ASSOCIATION_GATE = 6.0; // 关联波门阈值 6.0 度
    int M_HITS = 2;                // 【新增】：M/N 逻辑，累积命中 2 次即判定为真目标

    int internal_id_counter;       // 系统内部试探目标流水号
    int confirmed_target_count;    // 真正展示给用户的确实目标数量

    QList<TargetTrack> m_tracks;
};
