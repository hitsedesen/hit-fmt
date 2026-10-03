void Example()
{
    MAKE_TASK(value)
        .Schedule(worker)
        .Finish();
    MAKE_TASK_EXTRA(value).Schedule(worker).Finish();
    NExample::MAKE_TASK(value).Finish();
    NExample::MAKE_TASK_EXTRA(value).Finish();
    receiver.MAKE_TASK(value).Finish();
    auto task = MAKE_TASK(value)
                .Schedule(worker)
                .Finish();
    receiver.Get().Next().Finish();
    receiver.member.Finish();
    MAKE_TASK(value)
        .Schedule(worker)
        .Finish();
}
