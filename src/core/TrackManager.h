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

    // 【新增】：暴露接口供外部动态设定参数
    void setParameters(double assocGate, int mHits) {
        ASSOCIATION_GATE = assocGate;
        M_HITS = mHits;
    }

    void removeTrackById(int targetId);

private:
    double ASSOCIATION_GATE = 6.0; // 关联波门阈值
    int M_HITS = 10;               // M/N 逻辑，累积命中判定真目标

    int internal_id_counter;       // 系统内部试探目标流水号
    int confirmed_target_count;    // 真正展示给用户的确实目标数量

    QList<TargetTrack> m_tracks;
};
