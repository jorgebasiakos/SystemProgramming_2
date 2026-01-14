This project involves developing a multithreaded network job execution system in C on Linux, combining threads, sockets, and process control. The system consists of two main components:

jobExecutorServer: a network server managing job execution through a thread pool, enforcing concurrency limits, and using a shared buffer to queue incoming jobs from clients. Worker threads execute jobs using fork() and exec*() and return the output asynchronously to the requesting client.

jobCommander: a client interface that sends commands over the network to submit jobs, adjust concurrency, stop queued jobs, query job status, and terminate the server. Each command completes once the server responds with the relevant messages and job output.

Controller threads handle incoming client connections and commands, interacting with worker threads in a producer-consumer pattern, with proper synchronization using condition variables to avoid busy-waiting. The project emphasizes modular design, separate compilation, and adherence to Linux system programming practices.
