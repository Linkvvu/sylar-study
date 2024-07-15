      Poller ---> poll ready events
        ^                   ^
        |                   |
        | has a             | 确保每次到达的事件被线程安全的封装为协程
        |                   |
    IoManager ---> wrap all ready events as coroutine
        ^
        |
        | has a
        |
    Scheduler ---> schedule coroutines in thread-pool


