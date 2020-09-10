#pragma once

#include <chrono>

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include <plugin/Plugin.h>
#include "zeek_exporter.bif.h"

namespace plugin {
    namespace ESnet_Zeek_Exporter {

        class Plugin : public ::plugin::Plugin
        {
        public:
            Plugin();
            void InitPreScript() override;
            void InitPostScript() override;

        protected:
            // Overridden from plugin::Plugin.
            plugin::Configuration Configure() override;
            std::pair<bool, Val*> HookCallFunction(const Func* func, Frame* frame, val_list* args) override;
            bool HookLogWrite(const std::string& writer, const std::string& filter, const logging::WriterBackend::WriterInfo& info, int num_fields, const threading::Field* const* fields, threading::Value** vals) override;
            void MetaHookPre(HookType hook, const HookArgumentList& args) override;
            void MetaHookPost(HookType hook, const HookArgumentList& args, HookArgument result) override;

        private:
            void AddlArgumentPopulation(const char * name, val_list* args, std::map<std::string, std::string>& labels);
            unsigned long ScopeVariableSize(bool track_vars);

            const char* plugin_name = "ESnet::Zeek_Exporter";
            const char* node_name = getenv("CLUSTER_NODE") ? getenv("CLUSTER_NODE") : "standalone";

            // The current function depth. Used for time calculation and lineage.
            size_t func_depth = 0;
            // Track our parents
            std::vector<const char *> lineage;

            // In order to time how long function execution takes, we call the function ourselves (returning false to the plugin manager to indicate that we've taken over responsibility).
            // However, we want to provide other plugins a chance to run, so when the function gets called, hooks get executed again. To prevent recursing, we need to track some state
            // to tell if we're in our "outer" handler, or the "inner" handler.

            // If this is set, we're in the inner handler, but it could be our plugin or someone else's that's running.
            const Func* current_func = nullptr;

            // This determines whether it's our plugin running, or someone else's.
            bool own_handler = true;

            // These are for measuring the runtimes of hooks. We track them separately so that they don't clobber each other.
            // For example, if a log write happens within a function call, the log hook would clobber the function hook.
            std::chrono::steady_clock::time_point log_hook_start;
            std::chrono::steady_clock::time_point other_hook_start;

            // These are a stack to track children duration separately from parents.
            std::stack<std::chrono::steady_clock::time_point> func_hook_starts;

            // The duration of our CallFunction hook is the duration of the hook itself + the duration of the function call. We track
            // the function call durations here, so we can have an accurate duration of just the CallFunction hook.
            std::stack<std::chrono::microseconds> func_durations;

            // The duration of our function call is the duration of the function itself (the "absolute" time) + the duration
            // of any child functions called by the measured function. We keep track of those child function durations here,
            // so we can have an accurate "absolute" duration of just the function call.
            std::vector<double> child_func_durations;

            typedef std::tuple<int, int> offset_pair;
            // These events are those which we want to add more labels to, so we can track based on arguments.
            // Note: These should only be events with well-bounded arguments.
            //
            // The first int in the tuple is the offset in the var_list to store in the "arg" label.
            // The second int in the tuple is the offset in the var_list to store in the "addl" label.
            //
            // -1 offsets will not be stored.
            std::map<std::string, offset_pair> arg_events;

            // The data that we're exposing to Prometheus:
            std::shared_ptr<prometheus::Exposer> exposer;
            std::shared_ptr<prometheus::Registry> registry = std::make_shared<prometheus::Registry>();

            // Counter family for the number of log lines in zeek_log_writes_total
            prometheus::Family<prometheus::Counter>& zeek_log_writes_total = prometheus::BuildCounter()
                    .Name("zeek_log_writes_total")
                    .Help("The number of log writes per log, writer and filter.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // This family just tracks the start time of each plugin
            prometheus::Family<prometheus::Counter>& zeek_start_time_seconds = prometheus::BuildCounter()
                    .Name("zeek_start_time_seconds")
                    .Help("The epoch timestamp of when the process was started.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // Number of function calls, with labels for the function type (event, BIF, etc.), the function name, and the function parent (caller)
            prometheus::Family<prometheus::Counter>& zeek_function_calls_total = prometheus::BuildCounter()
                    .Name("zeek_function_calls_total")
                    .Help("The number of times Zeek functions were called, by function and function_caller")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The number of seconds spent in each function, by function type, function name and function_caller.
            prometheus::Family<prometheus::Counter>& zeek_cpu_time_per_function_seconds = prometheus::BuildCounter()
                    .Name("zeek_cpu_time_per_function_seconds")
                    .Help("The amount of time spent in Zeek functions. Measured in seconds.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The total size of all variables in each function, by function type, function name and function_caller.
            prometheus::Family<prometheus::Gauge>& zeek_var_size_per_function_bytes = prometheus::BuildGauge()
                    .Name("zeek_var_size_per_function_bytes")
                    .Help("The amount of memory usage of variables in Zeek functions. Measured in bytes.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The size of all variables in bytes, by variable name and scope.
            prometheus::Family<prometheus::Gauge>& zeek_var_size_bytes = prometheus::BuildGauge()
                    .Name("zeek_var_size_bytes")
                    .Help("The amount of memory usage of variables. Measured in bytes.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // We define "absolute" time as the amount of time the function took, minus the amount of time its children took.
            prometheus::Family<prometheus::Counter>& zeek_absolute_cpu_time_per_function_seconds = prometheus::BuildCounter()
                    .Name("zeek_absolute_cpu_time_per_function_seconds")
                    .Help("The \"absolute\" amount of time spent in Zeek functions. Note that these measurements DO NOT include the time spent in child functions. Measured in seconds.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The total amount of time spent in the functions, by function type.
            prometheus::Family<prometheus::Counter>& zeek_cpu_time_per_function_type_seconds = prometheus::BuildCounter()
                    .Name("zeek_cpu_time_per_function_type_seconds")
                    .Help("The amount of time spent in Zeek functions. Measured in seconds.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The seconds of CPU time consumed by the process as a whole.
            prometheus::Family<prometheus::Gauge>& zeek_total_cpu_time_seconds = prometheus::BuildGauge()
                    .Name("zeek_total_cpu_time_seconds")
                    .Help("The total amount of CPU time spent in this process")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The amount of time spent processing each plugin hook.
            prometheus::Family<prometheus::Counter>& zeek_hook_cpu_time_seconds = prometheus::BuildCounter()
                    .Name("zeek_hook_cpu_time_seconds")
                    .Help("The amount of time spent in Zeek plugin hooks. Measured in seconds.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

            // The number of times each plugin hook type was called.
            prometheus::Family<prometheus::Counter>& zeek_hooks_total = prometheus::BuildCounter()
                    .Name("zeek_hooks_total")
                    .Help("The number of times Zeek plugin hooks were called.")
                    .Labels({{"node", node_name}})
                    .Register(*registry);

        };

        extern Plugin plugin;

    }
}
