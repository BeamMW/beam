#pragma once
#include "timer.h"
#include <map>

namespace beam { namespace io {
    
/// Coarse timer helper, for connect/reconnect timers
class CoarseTimer {
public:
    using ID = uint64_t;
    using Callback = std::function<void(ID)>;
    using Ptr = std::unique_ptr<CoarseTimer>;
        
    /// Creates coarse timer, throws on errors
    static Ptr create(const Reactor::Ptr& reactor, unsigned resolutionMsec, const Callback& cb);
    
    /// Sets up timer callback for id, EC_EINVAL if id is already there or on timer setup failure
    expected<void, ErrorCode> set_timer(unsigned intervalMsec, ID id);
    
    /// Cancels callback for id
    void cancel(ID id);
    
    /// Cancels all callbacks
    void cancel_all();
                
private:
    CoarseTimer(unsigned resolutionMsec, const Callback& cb, Timer::Ptr&& timer);
    
    /// Internal callback
    void on_timer();
    
    /// abs. time
    using Clock = uint64_t;
   
    /// Updates timer after insertion
    void update_timer(Clock now);
           
    /// Coarse msec resolution
    const unsigned _resolution;
    
    /// External callback
    Callback _callback;
    
    /// Timers queue
    std::multimap<Clock, ID> _queue;
    
    /// Valid Ids
    std::map<ID, Clock> _validIds;
    
    /// Next time to wake
    Clock _nextTime=0;
    
    /// Timer object
    Timer::Ptr _timer;
};
    
}} //namespaces
