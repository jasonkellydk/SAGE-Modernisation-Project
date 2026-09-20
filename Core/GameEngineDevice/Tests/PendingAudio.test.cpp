#define BOOST_TEST_MODULE PendingAudioTests
#include <boost/test/included/unit_test.hpp>
#include "PendingAudio.h"

BOOST_AUTO_TEST_CASE(deferred_music_owns_filename_controls_and_can_be_cancelled)
{
    PendingAudioQueue queue;
    std::promise<bool> work;
    const auto job=work.get_future().share();
    std::string filename="music.mp3";
    AudioEvent event; event.filename=filename.c_str(); event.bus=AudioBus::Music;
    queue.Add(1,event,job);
    queue.Add(2,event,job);
    filename="overwritten";
    BOOST_CHECK(queue.TakeReady().empty());
    BOOST_REQUIRE(queue.Find(1));
    queue.Find(1)->paused=true;
    queue.Find(1)->event.volume=.25f;
    queue.Find(1)->event.pitchShift=.75f;
    BOOST_CHECK(queue.Remove(2));
    work.set_value(true);
    BOOST_CHECK(queue.TakeReady().empty());
    queue.Find(1)->paused=false;
    auto ready=queue.TakeReady();
    BOOST_REQUIRE_EQUAL(ready.size(),1u);
    BOOST_CHECK(ready.front().succeeded);
    BOOST_CHECK_EQUAL(ready.front().filename,"music.mp3");
    BOOST_CHECK_EQUAL(ready.front().event.volume,.25f);
    BOOST_CHECK_EQUAL(ready.front().event.pitchShift,.75f);
    BOOST_CHECK(!queue.Find(1));
    BOOST_CHECK(!queue.Find(2));
}

BOOST_AUTO_TEST_CASE(reset_and_failed_decodes_never_publish_stale_playback)
{
    PendingAudioQueue queue;
    std::promise<bool> cancelled,failed;
    const auto job=cancelled.get_future().share();
    AudioEvent event; event.filename="music.mp3";
    queue.Add(1,event,job);
    queue.Clear();
    cancelled.set_value(true);
    BOOST_CHECK(queue.TakeReady().empty());
    queue.Add(2,event,failed.get_future().share());
    failed.set_exception(std::make_exception_ptr(std::runtime_error("decode failure")));
    auto ready=queue.TakeReady();
    BOOST_REQUIRE_EQUAL(ready.size(),1u);
    BOOST_CHECK(!ready.front().succeeded);
    BOOST_CHECK(queue.TakeReady().empty());
}
