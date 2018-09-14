#include <assert.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "Channel.h"
#include "EPollPoller.h"
#include "EventLoop.h"
#include "TcpContext.h"

using namespace std;

__thread EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 10000;
const int MaxConnctionPoolSize = 256;

int createEventfd();

// 为保证线程安全，跨线程对象在构造函数期间不可把this指针泄露出去
// EventLoop不为跨线程对象，它与IO线程是一一对应关系
EventLoop::EventLoop()
    :looping_(false),
    eventHandling_(false),
    quit_(false),
    callingPendingFunctors_(false),
    wakeupFd_(createEventfd()),
    threadId_(CurrentThread::tid()),
    freeConnectionIndex_(0),
    currentActiveChannel_(NULL),
    poller_(new EPollPoller(this)),
    timerQueue_(new TimerQueue(this)),
    wakeupChannel_(new Channel(this, wakeupFd_)),
    tcpContext_(new std::vector<TcpContext>(MaxConnectionPoolSize)){
    // do some logging things
    // LOG_DEBUG
    if(t_loopInThisThread){
        // do some logging things
        // LOG_FATAL: another thread can access this eventloop
     }
    else{
        t_loopInThisThread = this;
    }
    
    // wake up the possibly blocked IO thread
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop(){
    assert(!looping_);

    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);

    t_loopInThisThread = NULL;
}

void EventLoop::loop(){
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    // quit_ = false;
    while(!quit_){
        activeChannels_.clear();
        poller_->poll(kPollTimeMs, &activeChannels_);

        eventHandling_ = true;
        for(ChannelList::iterator it = activeChannels_.begin(); it != activeChannels_.end(); ++it){
            currentActiveChannel_ = (*it).get();
            currentActiveChannel_->handleEvent();
        }
        currentActiveChannel_ = NULL;
        eventHandling_ = false;
        doPendingFunctors();
    }

    // LOG_TRACE
    looping_ = false;
}

void EventLoop::quit(){
    quit_ = true;
     if(!isInLoopThread()){
         wakeup();
     }
}

TimerId EventLoop::runAt(const Timestamp& time, TimerCallback&& cb){
    return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback&& cb){
    Timestamp time(addTime(Timestamp::now(), delay));
    return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback&& cb){
    Timestamp time(addTime(Timestamp::now(), interval));
    return timerQueue_->addTimer(std::move(cb), time, interval);
}


void EventLoop::runInLoop(Functor&& cb){
    if(isInLoopThread()){
        cb();
    }
    else{
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor&& cb){
    {
        MutexLockGuard lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    if(!isInLoopThread() || callingPendingFunctors_){
        wakeup();
    }
}


void EventLoop::wakeup(){
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    if(n != sizeof one){
        // LOG_ERROR;
    }
}

void EventLoop::handleRead(){
    uint64_t one = 1;
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one)){
        // LOG_ERROR
    }
}

void EventLoop::doPendingFunctors(){
    // narrow the critical area
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        MutexLockGuard lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for(size_t i = 0; i < functors.size(); i++){
        functors[i]();
    }

    callingPendingFunctors_ = false;
}


void EventLoop::updateChannel(Channel* channel){
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel){
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    
    // to do: take this channel being active channel or not into consideration
    if(eventHandling_){
        assert(currentActiveChannel_ == channel || std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
    }

    poller_->removeChannel(channel);
}


void EventLoop::abortNotInLoopThread(){
    // do some logging things
    // LOG_FATAL level
}

int createEventfd(){
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0){
        // LOG_SYSERR
        abort();
    }
    return evtfd;
}
