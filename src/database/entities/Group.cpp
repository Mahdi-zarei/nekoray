#include <include/database/entities/Group.h>

#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include <QRegularExpression> 

namespace Configs
{
    SubUserInfo ParseSubUserInfo(const QString &info) {
        SubUserInfo result;
        if (info.trimmed().isEmpty()) return result;

        static const QRegularExpression totalRe(R"((?:^|[;,\s])total=(\d+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression uploadRe(R"((?:^|[;,\s])upload=(\d+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression downloadRe(R"((?:^|[;,\s])download=(\d+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression expireRe(R"((?:^|[;,\s])expire=(\d+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression titleRe(R"((?:^|[;\s])title=([^;\n]+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression webUrlRe(R"((?:^|[;\s])(?:web_url|url)=([^;\n\s]+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression supportUrlRe(R"((?:^|[;\s])support_url=([^;\n\s]+))", QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression announceRe(R"((?:^|[;\s])announce=(.+)$)", QRegularExpression::CaseInsensitiveOption);

        auto mTotal = totalRe.match(info);
        if (mTotal.hasMatch()) {
            result.total = mTotal.captured(1).toLongLong();
            result.valid = true;
        }

        auto mUpload = uploadRe.match(info);
        if (mUpload.hasMatch()) {
            result.upload = mUpload.captured(1).toLongLong();
            result.valid = true;
        }

        auto mDownload = downloadRe.match(info);
        if (mDownload.hasMatch()) {
            result.download = mDownload.captured(1).toLongLong();
            result.valid = true;
        }

        auto mExpire = expireRe.match(info);
        if (mExpire.hasMatch()) {
            result.expire = mExpire.captured(1).toLongLong();
            if (result.expire > 1000000000000LL) {
                result.expire /= 1000; // Convert ms to seconds
            }
            result.valid = true;
        }

        auto mTitle = titleRe.match(info);
        if (mTitle.hasMatch()) {
            result.title = mTitle.captured(1).trimmed();
            result.valid = true;
        }

        auto mWeb = webUrlRe.match(info);
        if (mWeb.hasMatch()) {
            result.web_url = mWeb.captured(1).trimmed();
            result.valid = true;
        }

        auto mSup = supportUrlRe.match(info);
        if (mSup.hasMatch()) {
            result.support_url = mSup.captured(1).trimmed();
            result.valid = true;
        }

        auto mAnnounce = announceRe.match(info);
        if (mAnnounce.hasMatch()) {
            QString ann = mAnnounce.captured(1).trimmed();
            if (!ann.isEmpty() && ann.compare("base64:", Qt::CaseInsensitive) != 0) {
                result.announce = ann;
                result.valid = true;
            }
        }

        return result;
    }

    void Group::clearCalculatedColumnWidth() {
        calculated_column_width.clear();
    }

    QList<int> Group::Profiles() const {
        return profiles;
    }

    double bitrateToBps(const QString& str)
    {
        if (str.endsWith("Gbps", Qt::CaseInsensitive)) {
            double val = str.left(str.size() - 4).toDouble();
            return val * 1e9;
        }
        if (str.endsWith("Mbps", Qt::CaseInsensitive)) {
            double val = str.left(str.size() - 4).toDouble();
            return val * 1e6;
        }
        if (str.endsWith("Kbps", Qt::CaseInsensitive)) {
            double val = str.left(str.size() - 4).toDouble();
            return val * 1e3;
        }
        if (str == "N/A") return -1;
        return 0.0;
    }

    bool Group::SortProfiles(GroupSortAction sortAction) {
        if (!mutex.tryLock()) {
            return false;
        }
        auto allProfs = dataManager->profilesRepo->GetProfileBatch(profiles); // to warm up the cache
        switch (sortAction.method) {
            case GroupSortMethod::Raw: {
                break;
            }
            case GroupSortMethod::ById: {
                break;
            }
            case GroupSortMethod::ByAddress:
            case GroupSortMethod::ByName:
            case GroupSortMethod::ByTestResult:
            case GroupSortMethod::ByTraffic:
            case GroupSortMethod::BySecurity:
            case GroupSortMethod::ByType: {
                auto get_latency_for_sort = [](const std::shared_ptr<Profile>& prof) {
                    auto i = prof->latency;
                    if (i == 0) i = 100000;
                    if (i < 0) i = 99999;
                    return i;
                };
                std::ranges::sort(profiles,
                                  [&](int a, int b) {
                                      auto profA = dataManager->profilesRepo->GetProfile(a);
                                      auto profB = dataManager->profilesRepo->GetProfile(b);
                                      QString ms_a;
                                      QString ms_b;
                                      if (sortAction.method == GroupSortMethod::ByType) {
                                          ms_a = profA->outbound->DisplayType();
                                          ms_b = profB->outbound->DisplayType();
                                      } else if (sortAction.method == GroupSortMethod::ByName) {
                                          ms_a = profA->outbound->name;
                                          ms_b = profB->outbound->name;
                                      } else if (sortAction.method == GroupSortMethod::ByAddress) {
                                          ms_a = profA->outbound->DisplayAddress();
                                          ms_b = profB->outbound->DisplayAddress();
                                      } else if (sortAction.method == GroupSortMethod::BySecurity) {
                                          auto secA = profA->outbound->GetSecurity();
                                          auto secB = profB->outbound->GetSecurity();
                                          if (secA.level != secB.level) {
                                              return sortAction.descending ? secA.level > secB.level
                                                                           : secA.level < secB.level;
                                          }
                                          ms_a = secA.transport + secA.label;
                                          ms_b = secB.transport + secB.label;
                                      } else if (sortAction.method == GroupSortMethod::ByTestResult) {
                                          if (test_sort_by == testBy::latency) {
                                              return sortAction.descending ? get_latency_for_sort(profA) > get_latency_for_sort(profB) : get_latency_for_sort(profA) < get_latency_for_sort(profB);
                                          }
                                          if (test_sort_by == testBy::dlSpeed) {
                                              return sortAction.descending ? bitrateToBps(profA->dl_speed) > bitrateToBps(profB->dl_speed) : bitrateToBps(profA->dl_speed) < bitrateToBps(profB->dl_speed);
                                          }
                                          if (test_sort_by == testBy::ulSpeed) {
                                              return sortAction.descending ? bitrateToBps(profA->ul_speed) > bitrateToBps(profB->ul_speed) : bitrateToBps(profA->ul_speed) < bitrateToBps(profB->ul_speed);
                                          }
                                          if (test_sort_by == testBy::ipOut) {
                                              return sortAction.descending ? profA->ip_out > profB->ip_out : profA->ip_out < profB->ip_out;
                                          }
                                      } else if (sortAction.method == GroupSortMethod::ByTraffic) {
                                          if (traffic_sort_by == trafficBy::total) {
                                              auto totalA = profA->traffic_downlink + profA->traffic_uplink;
                                              auto totalB = profB->traffic_downlink + profB->traffic_uplink;
                                              return sortAction.descending ? totalA > totalB  : totalA < totalB;
                                          }
                                          if (traffic_sort_by == trafficBy::dl) {
                                              return sortAction.descending ? profA->traffic_downlink > profB->traffic_downlink : profA->traffic_downlink < profB->traffic_downlink;
                                          }
                                          if (traffic_sort_by == trafficBy::ul) {
                                              return sortAction.descending ? profA->traffic_uplink > profB->traffic_uplink : profA->traffic_uplink < profB->traffic_uplink;
                                          }
                                      }
                                      return sortAction.descending ? ms_a > ms_b : ms_a < ms_b;
                                  });
                break;
            }
        }
        mutex.unlock();
        return true;
    }

    bool Group::AddProfile(int ID)
    {
        QMutexLocker locker(&mutex);
        if (HasProfile(ID))
        {
            return false;
        }
        profiles.append(ID);
        return true;
    }

    bool Group::AddProfileBatch(const QList<int>& IDs) {
        QSet<int> currentProfiles;
        for (const auto& profileID : profiles) {
            currentProfiles.insert(profileID);
        }
        QMutexLocker locker(&mutex);
        for (auto profileID : IDs) {
            if (!currentProfiles.contains(profileID)) {
                profiles.append(profileID);
            }
        }
        return true;
    }

    bool Group::RemoveProfile(int ID)
    {
        QMutexLocker locker(&mutex);
        if (!HasProfile(ID)) return false;
        profiles.removeAll(ID);
        return true;
    }

    bool Group::RemoveProfileBatch(const QList<int>& IDs) {
        QSet<int> toDel;
        for (auto ID : IDs) {
            toDel.insert(ID);
        }
        QList<int> newIDs;
        QMutexLocker locker(&mutex);
        for (auto inID : profiles) {
            if (!toDel.contains(inID)) {
                newIDs.append(inID);
            }
        }
        profiles = newIDs;
        return true;
    }

    bool Group::SwapProfiles(int idx1, int idx2)
    {
        QMutexLocker locker(&mutex);
        if (profiles.size() <= idx1 || profiles.size() <= idx2) return false;
        profiles.swapItemsAt(idx1, idx2);
        return true;
    }

    bool Group::EmplaceProfile(int idx, int newIdx)
    {
        QMutexLocker locker(&mutex);
        if (profiles.size() <= idx || profiles.size() <= newIdx) return false;
        profiles.insert(newIdx+1, profiles[idx]);
        if (idx < newIdx) profiles.remove(idx);
        else profiles.remove(idx+1);
        return true;
    }

    bool Group::HasProfile(int ID) const
    {
        return profiles.contains(ID);
    }
}
