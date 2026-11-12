void configure() {
    if (enabled) {
        channel.Queue =
            Make<ConcurrentTaskQueue>(
                notificationCounter,
                queueDescription
                    .GroupTags,
                queueDescription
                    .ProfilingTags,
                registry);
        channel->Queue =
            Make<TaskQueue>(
                notificationCounter,
                queueDescription
                    .GroupTags,
                queueDescription
                    .ProfilingTags,
                registry);
        channel.State.Queue =
            Make<TaskQueue>(
                notificationCounter,
                queueDescription
                    .GroupTags,
                queueDescription
                    .ProfilingTags,
                registry);
        channel.Queue +=
            Make<TaskQueue>(
                notificationCounter,
                queueDescription
                    .GroupTags,
                queueDescription
                    .ProfilingTags,
                registry);
        channel.Queue = Make<TaskQueue>(
            notificationCounter,
            queueDescription.GroupTags,
            queueDescription
                .ProfilingTags,
            registry);
        channel.Queue = Make<TaskQueue>(
            one,
            two);
        destinationWithAnExceptionallyLongName
            .Queue = Make<TaskQueue>(
                one,
                two);
        channel.Queue = source.collect(
            notificationCounter,
            queueDescription.GroupTags,
            queueDescription
                .ProfilingTags,
            registry);
        consume(
            channel.Queue,
            queueDescription.GroupTags,
            queueDescription
                .ProfilingTags,
            notificationCounter,
            registry);
    }
}
