---
content_title: Logging Levels
---

There are six available logging levels:
- all
- debug
- info
- warn
- error
- off  

Sample `logging.json`:

```
{
 "includes": [],
 "appenders": [{
     "name": "consoleout", 
     "type": "console",
     "args": {
       "stream": "std_out",
       "level_colors": [{
           "level": "debug",
           "color": "green"
         },{
           "level": "warn",
           "color": "brown"
         },{
           "level": "error",
           "color": "red"
         }
       ]
     },
     "enabled": true
   },{
     "name": "net",
     "type": "gelf",
     "args": {
       "endpoint": "10.10.10.10",
       "host": "test"
     },
     "enabled": true
   }
 ],
 "loggers": [{
     "name": "default",
     "level": "info",
     "enabled": true,
     "additivity": false,
     "appenders": [
       "consoleout",
       "net"
     ]
   },{
     "name": "net_plugin_impl",
     "level": "debug",
     "enabled": true,
     "additivity": false,
     "appenders": [
       "net"
     ]
   }
 ]
}
```

## Expected output of log levels

* `error`
  * Log output that likely requires operator intervention. Error level logging should 
    be reserved for conditions that are completely unexpected or otherwise needs human
    intervention. Also used to indicate a software error like impossible values of an
    `enum`, out of bounds array access, null pointers, or other conditions that likely
    will throw an exception. Currently, there are numerous `error` level logging 
    that likely should be `warn` as they do not require human intervention. The 
    `net_plugin_impl`, for example, has a number of `error` level logs for bad network 
    connections. This is handled and processed correctly. These should be changed 
    to `warn` or `info`.
* `warn`
  * Log output indicating unexpected but recoverable errors. Although, `warn` level
    typically does not require human intervention, repeated output of `warn`
    level logs may indicate action is needed by an operator. `warn` should not be used
    for simply conveying information. A `warn` level log is something to take notice
    of, but not necessarily be concerned about.
* `info` (default)
  * Log output that provides useful information to an operator. Can just be progress
    indication or other useful data to a user. Care is taken not to create excessive
    log output with `info` level logging. For example, no `info` level logging should
    be produced for every transaction. For progress indication, some multiple of
    transactions should be processed between each log output; typically, every 1000
    transactions.
* `debug`
  * Useful log output for when non-default logging is enabled. Answers the question:
    is this useful information for a user that is monitoring the log output. Care
    should be taken to not create excessive log output; similar to `info` level
    logging. `debug` level should not be used for `trace` level logging. Enabling
    `debug` level logging should provide greater insight into behavior without
    overwhelming the output with log entries. Like `info`, no `debug` level
    logging should be produced for every transaction. There are specific 
    transaction level loggers dedicated to transaction level logging: `transaction`, 
    `transaction_trace_failure`, `transaction_trace_success`, `transaction_failure_tracing`,
    `transaction_success_tracing`.
* `all` (trace)
  * `all` level logging is for logging that would be overwhelming if it was `debug`
    level logging. Only used in a few places and not completely supported. In the
    future a different logging library may provide better trace level logging support.
    The current logging framework is not performant enough to enable excess trace
    level output.