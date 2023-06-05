## Rules and considerations for writing an appbase application

### Appbase application lifetime

#### Plugin registration

* Register plugins by calling `appbase::application::register_plugin<plugin_name>();`. This needs to be done only once per executable.
* These plugins, and their dependents, will get registered when `initialize` is called on `appbase::scoped_app`
* plugin registration is required for every plugin we want to use.

#### Plugin initialization

* Precondition: plugins must have been registered
* Happens when `appbase::scoped_app::initialize(argc, argv)` is called
* Plugins to be initialized are those passed as options (using the `--plugin` command line option) or as template parameters  to the `initialize()` call, as well as their dependents.
* if we try to initialize an unregistered plugin, we get an exception (`std::exception::what: unable to find plugin`).
* initialization order: 
    - for each plugin specified in command line, initialize its dependents (depth first) and then itself
    - for each plugin specified as template parameter, initialize its dependents (depth first) and then itself
* Plugin state is updated to `initialized` *before* its descendents and itself are initialized. 
  So if there was a dependency cycle, the same plugin would not be initialized twice.
* After the plugin initialization, `app().plugin_initialized(this);` is called which allows appbase to add it to its list of initialized plugins.


#### Application initialization

Currently, `appbase::initialize()` can:

1. either return `false` 
     - if a specified option like `--help` or `--version` is specified, which does not require to continue the program execution
     - if a configuration error is detected (for ex `--logconf` specified dir missing, `--config` file missing)
2. or throw in case of failure 
     - if any plugin throws during `plugin_initialize`, `appbase::initialize()` logs the error and rethrows the exception
3. or return `true` is initialization was successful and the application should proceed.
     
     
So an application should exit cleanly in cases 1 and 2 (without calling `app->startup();` or `app->exec();`), and continue in case 3.

Unfortunately, when `appbase::initialize()` returns false, it is not clear whether it indicates a normal exit or an error, so for example in `nodeos` we do:


```c++
   try {
      ...
      if(!app->initialize<chain_plugin, net_plugin, producer_plugin, resource_monitor_plugin>(argc, argv, initialize_logging)) {
         const auto& opts = app->get_options();
         if( opts.count("help") || opts.count("version") || opts.count("full-version") || opts.count("print-default-config") ) {
            return SUCCESS;
         }
         return INITIALIZE_FAIL;
      }
      ...
      app->startup();
      app->exec();
   }
   // other catch blocks
   } catch( ... ) {
      elog("unknown exception");
      return OTHER_FAIL;
   }
```

> Note that in the code above, we check the return value of `app->initialize()` (as we should) and we have a try/catch block in case `app->initialize()` (or any of the plugins) throws an exception

#### Application Startup

This is when `appbase::scoped_app::startup()` is called. Besides starting a thread for catching signals, the main task is to call `startup() on all initialized plugins. 


```c++
   try {
      for( auto plugin : initialized_plugins ) {
         if( is_quiting() ) break;
         plugin->startup();
      }

   } catch( ... ) {
      clean_up_signal_thread();
      shutdown();
      throw;
   }
```

If any plugin throws during plugin_startup, the startup process stops and an orderly shutdown is initiated, where all started plugins (including the one that threw) will be invoked with plugin_shutdown. After the orderly shutdown of all started plugins, the exception is rethrown, and it is expected that this exception will cause the application to terminate without calling `app->exec()`.

If a plugin calls `app().quit()` during plugin_startup (not recommended), the appbase framework will detect it and throw an exception, so we fall back to the same processing as in the above paragraph.

> We note that plugins are also started in a similar way as C++ objects are constructed, where the dependent plugins are started first, in a depth first fashion. Just like C++, shutdown proceeds in the reverse order.


## Rules and considerations for writing plugins


### Initialize

#### Typical actions in `plugin_initialize`

- get references (using `app().find_plugin<name>()`) to other plugins you depend on, and maybe store a reference to them.
  Those dependencies should have been specified in the plugin class definition using `APPBASE_PLUGIN_REQUIRES`
- use the provided program options to configure the plugin
- possibly read additional configuration from files
- create new objects, directories, files, etc... 
  critically any deallocation/cleanup of these (or other objects created during the plugin execution) should be done in the plugin destructor, not in `plugin_shutdown()`.
- connect to signals from other plugins (ex: `chain.applied_transaction.connect([this](...) { ... });`)
- register provider methods (that others can connect to via signals). 
  Ex: `app().get_method<incoming::methods::transaction_async>().register_provider([this](...) { ... });`
- relay signals to channels.
  Ex: `pre_accepted_block_connection = chain->pre_accepted_block.connect([this](...) { ...; pre_accepted_block_channel.publish(...); });`

#### Rules to follow in `plugin_initialize`

- in case of irrecoverable error, appropriate action is to throw an exception (possibly after logging the error). 
  This will cause the application to terminate.
- `io_context`s should not be running, and threads should not be started that would run on any `io_context` before `plugin_startup`.
- plugins should not post any action involving callbacks (timer, io, etc), either on the main appbbase thread or on plugin-specific thread pools, during `plugin_initialize`.

#### Guarantees provided by appbase to `plugin_initialize`

- dependent plugins (as specified by `APPBASE_PLUGIN_REQUIRES`) are always initialized before the plugins that depends on them.
- if any plugin throws during `plugin_initialize`, the plugin initialization process stops immediately, an error is logged, and the exception is rethrown. `plugin_shutdown` is not called for any plugin, the expectation being that any object or resource that was allocated during `plugin_initialize` will be freed when the plugin destructor is executed.



### Startup

`plugin_startup()` is intended to allow the plugins to activate their `io_context`s, thread pools, timers, establish connections with dependent plugins, and start the intended processing of the plugin, in order to make sure that all the resources necessary are available. If critical resources  required for the application correct functioning are not available, the plugin should throw an exception.

Ideally, all resource allocation is completed before `plugin_startup` returns. 

If the plugin was to start a thread, which allocates resources upon startup, this could trigger an exception while another plugin is executing `plugin_startup`, making the log harder to decipher as it would intermingle messages from different sources.

> in some case, a delayed startup is desired, because we would like other plugins to finish their startup before we start our own services (for example the http_plugin may want to start listening for API requests only after the blockchain replay is completed). One way to achieve this is to post a lambda implementing the plugin startup on the appbase queue, as in: `app().executor().post(appbase::priority::high, [this] () { ... });`

#### Typical actions in `plugin_startup`

Example of actions in `plugin_startup`:

- an `io_context`, task queue and a main thread is provided by appbase, and plugins can schedule tasks to be executed on it using `app().executor().post()`. However, if necessary, plugins can create their own `io_context` and thread pools, and `plugin_startup()` is an appropriate time to activate these.
- if the plugin starts its own thread pool, and these threads allocate resources upon startup, it is recommented for the plugin to wait till all the threads are ready to accept work (using a synchronization primitive such as a `condition_variable`) before returning from `plugin_startup`.
- a plugin which communicates with other nodes may want to open network connections, or start listening for incoming connections.


#### Rules to follow in `plugin_startup`

- in case of an error during `plugin_startup()`, the plugin should throw, not call `app().quit()`
- any code in `plugin_shutdown()` should not depend on `plugin_startup` having fully executed.

#### Guarantees provided by appbase to `plugin_startup`

- `plugin_startup()` is called in a depth-first fashion, according to the stated dependency graph specified by `APPBASE_PLUGIN_REQUIRES`.
- `plugin_startup()` is called within a try/catch block. 
- If an exception is thrown while `plugin_startup()` is executed on any plugin, `plugin_shutdown()` is  called on it immediately, and an application shutdown is triggerred.
- but if the plugin does call `app().quit()` during `plugin_startup()`, the framework will throw, ensuring no other plugin is started and application terminates cleanly.
- Before `plugin_startup()` is called, the plugin is added to the list of running plugins, which ensures that `plugin_shutdown()` will be called on it (either at application shutdown, or in case of an exception occuring before that)
- because  `plugin_shutdown()` will be executed regardless of whether `plugin_startup()` fully (or at all) executed, it is critical that `plugin_shutdown()`'s actions do not require or expect anything from `plugin_startup()`. For example, calling `cancel()` on a timer that has not been `async_wait`'ed on is fine.



### Shutdown

#### Typical actions in `plugin_shutdown`

`plugin_shutdown()` is intended to allow the plugins to terminate its processing, for example:

- cancel timers (typically `boost::asio::steady_timer` or `boost::asio::deadline_timer`
- stop thread pools (stop the `io_context`, join all threads)
- disconnect from other plugins if needed (this is probably not needed).

#### Rules to follow in `plugin_shutdown`

- plugin created objects, or the plugin itself, should *not* be destroyed during `plugin_shutdown()`. 
  This must be done in the plugin destructor.

#### Guarantees provided by appbase to `plugin_shutdown`

- `plugin_shutdown()` is called within a try/catch block for each running plugin, starting from the last registered first.
  This means that `plugin_shutdown()` is called on the inverse order of `plugin_startup()`, 
- If an exception is thrown during `plugin_shutdown()`, it is logged on `std::cerr`, but doesn't prevent `plugin_shutdown()` from being called on the remaining running plugins.
- The appbase framework will destruct the running plugins in a second phase of shutdown (last one added to the running list is destructed first).


## Action items

1. If a plugin (or one of its dependents) throws during `startup()`, `plugin_shutdown()` should be called on it.
    => update producer_plugin and other plugins which may (needlessly) call `plugin_shutdown()` when there is an exception in  `plugin_startup()`
    
```
    **done** [appbase 65bb056] ensure plugin_shutdown called when plugin_shutdown throws.
             [leap/gh-672 3210dfe48] net_plugin:  no need to catch exceptions to make sure `plugin_shutdown()` is executed.
             [leap/gh-672 dc72da4e5] producer_plugin: remove unnecessary try/catch block calling `plugin_shutdown()`
```

2. many appbase apps do not check the return value of `app->initialize()` or catch exceptions.

3. have `app->initialize()` return `enum class result { all_done, success, failure }`, so an application knows whether to just return or contunue with `app->startup();`.




