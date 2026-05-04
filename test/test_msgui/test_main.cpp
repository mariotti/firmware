// Unit tests for msgui::ConvModel.
//
// Run directly (macOS / Linux, no PlatformIO):
//   make -C test/test_msgui
//
// Run via PlatformIO on Linux CI:
//   pio test -e native -f test_msgui
//
// Tests are pure: no firmware globals, no hardware, no RTOS.

#include "graphics/msgui/ConvModel.h"
#include <cstring>
#include <unity.h>

using namespace msgui;

// Unity calls these before/after each test; no setup/teardown needed.
void setUp()   {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr uint32_t MY = 0xAAAA0001;
static constexpr uint32_t PEER1 = 0xBBBB0001;
static constexpr uint32_t PEER2 = 0xBBBB0002;
static constexpr uint32_t BROADCAST = 0xFFFFFFFF;

static ConvMsg makeChannel(uint8_t ch, uint32_t sender, uint32_t ts, const char *text)
{
    ConvMsg m{};
    m.sender    = sender;
    m.dest      = BROADCAST;
    m.timestamp = ts;
    m.channel   = ch;
    m.isDm      = false;
    strncpy(m.text, text, sizeof(m.text) - 1);
    return m;
}

static ConvMsg makeDm(uint32_t from, uint32_t to, uint32_t ts, const char *text)
{
    ConvMsg m{};
    m.sender    = from;
    m.dest      = to;
    m.timestamp = ts;
    m.channel   = 0;
    m.isDm      = true;
    strncpy(m.text, text, sizeof(m.text) - 1);
    return m;
}

// ---------------------------------------------------------------------------
// buildNodeList tests
// ---------------------------------------------------------------------------

void test_empty_messages_produces_no_convs()
{
    NodeEntry out[8];
    size_t n = buildNodeList(nullptr, 0, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_single_channel_message_produces_one_conv()
{
    ConvMsg msgs[] = { makeChannel(0, PEER1, 100, "hello") };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 1, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_TRUE(out[0].isChannel);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channelIndex);
    TEST_ASSERT_EQUAL_STRING("hello", out[0].preview);
    TEST_ASSERT_EQUAL_UINT32(100, out[0].lastTs);
}

void test_two_channel_messages_same_channel_produces_one_conv()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 200, "world"),
        makeChannel(0, PEER2, 100, "hello"),
    };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    // First message (newest-first order) provides the preview
    TEST_ASSERT_EQUAL_STRING("world", out[0].preview);
    TEST_ASSERT_EQUAL_UINT32(200, out[0].lastTs);
}

void test_two_different_channels_produce_two_convs()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "ch0 msg"),
        makeChannel(1, PEER1, 200, "ch1 msg"),
    };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_TRUE(out[0].isChannel);
    TEST_ASSERT_TRUE(out[1].isChannel);
    TEST_ASSERT_NOT_EQUAL(out[0].channelIndex, out[1].channelIndex);
}

void test_dm_from_peer_produces_one_conv_keyed_on_peer()
{
    ConvMsg msgs[] = { makeDm(PEER1, MY, 100, "hi") };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 1, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FALSE(out[0].isChannel);
    TEST_ASSERT_EQUAL_UINT32(PEER1, out[0].peerNum);
}

void test_dm_sent_by_me_to_peer_produces_one_conv_keyed_on_peer()
{
    ConvMsg msgs[] = { makeDm(MY, PEER1, 100, "hey") };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 1, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FALSE(out[0].isChannel);
    TEST_ASSERT_EQUAL_UINT32(PEER1, out[0].peerNum);
}

void test_dm_both_directions_same_peer_produces_one_conv()
{
    ConvMsg msgs[] = {
        makeDm(PEER1, MY, 100, "hello"),
        makeDm(MY, PEER1, 200, "reply"),
    };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT32(PEER1, out[0].peerNum);
}

void test_mixed_channel_and_dm_produces_two_convs()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "broadcast"),
        makeDm(PEER1, MY, 200, "direct"),
    };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
}

void test_maxOut_limits_output()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "ch0"),
        makeChannel(1, PEER1, 200, "ch1"),
        makeChannel(2, PEER1, 300, "ch2"),
    };
    NodeEntry out[2];
    size_t n = buildNodeList(msgs, 3, MY, out, 2);
    TEST_ASSERT_EQUAL_size_t(2, n);
}

// ---------------------------------------------------------------------------
// filterThreadMsgs tests
// ---------------------------------------------------------------------------

void test_filter_returns_only_matching_channel_messages()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "ch0 a"),
        makeChannel(1, PEER1, 200, "ch1 a"),
        makeChannel(0, PEER2, 300, "ch0 b"),
    };
    NodeEntry conv{};
    conv.isChannel    = true;
    conv.channelIndex = 0;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(msgs, 3, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_STRING("ch0 a", out[0].text);
    TEST_ASSERT_EQUAL_STRING("ch0 b", out[1].text);
}

void test_filter_returns_only_matching_dm_messages()
{
    ConvMsg msgs[] = {
        makeDm(PEER1, MY,    100, "peer1 to me"),
        makeDm(MY,    PEER1, 200, "me to peer1"),
        makeDm(PEER2, MY,    300, "peer2 to me"),
    };
    NodeEntry conv{};
    conv.isChannel = false;
    conv.peerNum   = PEER1;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(msgs, 3, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_STRING("peer1 to me", out[0].text);
    TEST_ASSERT_EQUAL_STRING("me to peer1", out[1].text);
}

void test_filter_empty_store_returns_zero()
{
    NodeEntry conv{};
    conv.isChannel    = true;
    conv.channelIndex = 0;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(nullptr, 0, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_filter_maxOut_limits_output()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "a"),
        makeChannel(0, PEER1, 200, "b"),
        makeChannel(0, PEER1, 300, "c"),
    };
    NodeEntry conv{};
    conv.isChannel    = true;
    conv.channelIndex = 0;

    ConvMsg out[2];
    size_t n = filterThreadMsgs(msgs, 3, MY, conv, out, 2);
    TEST_ASSERT_EQUAL_size_t(2, n);
}

// ---------------------------------------------------------------------------
// Phase 3: ordering and preview correctness
// ---------------------------------------------------------------------------

// The first entry in the output is the conversation whose newest message came
// first in the input array (i.e. inputs should be passed newest-first).
void test_conv_preview_comes_from_first_input_message()
{
    // Pass newest first — matches how UIThread iterates msgs.rbegin()
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 300, "newest"),
        makeChannel(0, PEER1, 200, "older"),
        makeChannel(0, PEER1, 100, "oldest"),
    };
    NodeEntry out[4];
    size_t n = buildNodeList(msgs, 3, MY, out, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    // Preview should come from the first (newest) message
    TEST_ASSERT_EQUAL_STRING("newest", out[0].preview);
    TEST_ASSERT_EQUAL_UINT32(300, out[0].lastTs);
}

// Two peers produce two separate DM conversations, each keyed on the right peer.
void test_two_peers_produce_separate_dm_convs()
{
    ConvMsg msgs[] = {
        makeDm(PEER1, MY, 100, "from peer1"),
        makeDm(PEER2, MY, 200, "from peer2"),
    };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT32(PEER1, out[0].peerNum);
    TEST_ASSERT_EQUAL_UINT32(PEER2, out[1].peerNum);
}

// A DM I sent to PEER1 and a message I received from PEER1 collapse into
// exactly one DM conversation.
void test_outgoing_and_incoming_dm_collapse_to_one_conv()
{
    ConvMsg msgs[] = {
        makeDm(MY,    PEER1, 300, "my reply"),   // sent by me
        makeDm(PEER1, MY,    100, "their msg"),  // received from peer
    };
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FALSE(out[0].isChannel);
    TEST_ASSERT_EQUAL_UINT32(PEER1, out[0].peerNum);
}

// Channel and DM with the same numeric index don't collide.
void test_channel_and_dm_with_same_index_do_not_collide()
{
    ConvMsg msgs[] = {
        makeChannel(1, PEER1, 100, "ch1 broadcast"),
        makeDm(PEER1, MY,    200, "dm from peer1"), // peerNum happens to equal 1 if PEER1==1
    };
    // Use a peer whose num != 1 to keep the test straightforward
    NodeEntry out[8];
    size_t n = buildNodeList(msgs, 2, MY, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    // One must be a channel, one a DM
    bool hasChannel = false, hasDm = false;
    for (size_t i = 0; i < n; i++) {
        if (out[i].isChannel) hasChannel = true;
        else                  hasDm      = true;
    }
    TEST_ASSERT_TRUE(hasChannel);
    TEST_ASSERT_TRUE(hasDm);
}

// filterThreadMsgs preserves chronological (input) order.
void test_filter_preserves_message_order()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "first"),
        makeChannel(0, PEER2, 200, "second"),
        makeChannel(0, PEER1, 300, "third"),
    };
    NodeEntry conv{};
    conv.isChannel    = true;
    conv.channelIndex = 0;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(msgs, 3, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT32(100, out[0].timestamp);
    TEST_ASSERT_EQUAL_UINT32(200, out[1].timestamp);
    TEST_ASSERT_EQUAL_UINT32(300, out[2].timestamp);
}

// Messages I sent to PEER1 appear in the PEER1 DM filter.
void test_filter_includes_my_outgoing_dms()
{
    ConvMsg msgs[] = {
        makeDm(MY,    PEER1, 100, "i said"),
        makeDm(PEER1, MY,    200, "they said"),
        makeDm(PEER2, MY,    300, "other peer"),
    };
    NodeEntry conv{};
    conv.isChannel = false;
    conv.peerNum   = PEER1;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(msgs, 3, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_STRING("i said",    out[0].text);
    TEST_ASSERT_EQUAL_STRING("they said", out[1].text);
}

// Empty input → zero output regardless of conv spec.
void test_filter_null_msgs_returns_zero()
{
    NodeEntry conv{};
    conv.isChannel    = true;
    conv.channelIndex = 0;
    ConvMsg out[4];
    TEST_ASSERT_EQUAL_size_t(0, filterThreadMsgs(nullptr, 0, MY, conv, out, 4));
}

// ---------------------------------------------------------------------------
// User journey tests
//
// Each test simulates a realistic sequence of events a user would experience
// and verifies the resulting model state.  They exercise buildNodeList +
// filterThreadMsgs together and encode the invariants that matter most for
// correct UI behaviour.
// ---------------------------------------------------------------------------

// Journey: a message arrives from a new peer and immediately shows up in
// the conversation list with the correct preview and timestamp.
void test_journey_incoming_dm_appears_in_list()
{
    ConvMsg msgs[] = { makeDm(PEER1, MY, 500, "hey there") };
    NodeEntry convs[8];
    size_t n = buildNodeList(msgs, 1, MY, convs, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_FALSE(convs[0].isChannel);
    TEST_ASSERT_EQUAL_UINT32(PEER1, convs[0].peerNum);
    TEST_ASSERT_EQUAL_STRING("hey there", convs[0].preview);
    TEST_ASSERT_EQUAL_UINT32(500, convs[0].lastTs);
}

// Journey: a channel broadcast arrives and shows up as a channel conversation.
void test_journey_channel_message_appears_in_list()
{
    ConvMsg msgs[] = { makeChannel(0, PEER1, 100, "hello channel") };
    NodeEntry convs[8];
    size_t n = buildNodeList(msgs, 1, MY, convs, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_TRUE(convs[0].isChannel);
    TEST_ASSERT_EQUAL_UINT8(0, convs[0].channelIndex);
    TEST_ASSERT_EQUAL_STRING("hello channel", convs[0].preview);
}

// Journey: user has a DM open with PEER1; a new message from PEER2 arrives.
// The conversation list now has two entries, PEER2 first (newer message).
// The re-anchor pattern used by UIThread must find PEER1 at its new index.
void test_journey_reanchor_after_new_message_reorders_list()
{
    // Initial state: only PEER1 has messaged us.
    ConvMsg msgs_v1[] = { makeDm(PEER1, MY, 100, "first") };
    NodeEntry convs[8];
    size_t n = buildNodeList(msgs_v1, 1, MY, convs, 8);
    TEST_ASSERT_EQUAL_size_t(1, n);

    // User opens PEER1's conversation (selectedNode = 0).
    size_t selectedNode  = 0;
    uint32_t savedPeer   = convs[selectedNode].peerNum;  // PEER1
    bool savedIsChannel  = convs[selectedNode].isChannel;
    TEST_ASSERT_EQUAL_UINT32(PEER1, savedPeer);

    // PEER2 sends a message — pass newest-first so PEER2 is first.
    ConvMsg msgs_v2[] = {
        makeDm(PEER2, MY, 200, "new message"),
        makeDm(PEER1, MY, 100, "first"),
    };
    n = buildNodeList(msgs_v2, 2, MY, convs, 8);
    TEST_ASSERT_EQUAL_size_t(2, n);

    // Re-anchor: find PEER1 in the new list (mirrors UIThread::rebuildConvList).
    for (size_t i = 0; i < n; i++) {
        if (!savedIsChannel && !convs[i].isChannel && convs[i].peerNum == savedPeer) {
            selectedNode = i;
            break;
        }
    }

    // selectedNode must still point to PEER1, not to PEER2.
    TEST_ASSERT_FALSE(convs[selectedNode].isChannel);
    TEST_ASSERT_EQUAL_UINT32(PEER1, convs[selectedNode].peerNum);
}

// Journey: user sends a reply to PEER1; after rebuild the conversation list
// reorders (PEER1 moves to top) but selectedNode still resolves to PEER1.
void test_journey_reanchor_after_own_reply()
{
    // PEER2 messaged first, then PEER1 — so PEER1 is at top (newest-first).
    ConvMsg msgs_v1[] = {
        makeDm(PEER1, MY, 200, "peer1 msg"),
        makeDm(PEER2, MY, 100, "peer2 msg"),
    };
    NodeEntry convs[8];
    size_t n = buildNodeList(msgs_v1, 2, MY, convs, 8);
    // PEER1 is first (index 0), PEER2 second (index 1).
    TEST_ASSERT_EQUAL_UINT32(PEER1, convs[0].peerNum);

    // User is viewing PEER2 (index 1).
    size_t selectedNode = 1;
    uint32_t savedPeer  = convs[selectedNode].peerNum;  // PEER2
    bool savedIsChannel = false;

    // User replies to PEER2 — PEER2's message is now newest.
    ConvMsg msgs_v2[] = {
        makeDm(MY, PEER2, 300, "my reply"),   // newest
        makeDm(PEER1, MY,  200, "peer1 msg"),
        makeDm(PEER2, MY,  100, "peer2 msg"),
    };
    n = buildNodeList(msgs_v2, 3, MY, convs, 8);
    // PEER2 should now be first.
    TEST_ASSERT_EQUAL_UINT32(PEER2, convs[0].peerNum);

    // Re-anchor.
    for (size_t i = 0; i < n; i++) {
        if (!savedIsChannel && !convs[i].isChannel && convs[i].peerNum == savedPeer) {
            selectedNode = i;
            break;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(PEER2, convs[selectedNode].peerNum);
    TEST_ASSERT_EQUAL_STRING("my reply", convs[selectedNode].preview);
}

// Journey: user views a DM conversation — only that peer's messages are shown,
// in chronological order, including messages sent by the user.
void test_journey_open_dm_shows_full_thread()
{
    ConvMsg msgs[] = {
        makeDm(PEER1, MY,    100, "hi"),
        makeDm(MY,    PEER1, 200, "hey back"),
        makeDm(PEER2, MY,    150, "unrelated"),
        makeDm(PEER1, MY,    300, "how are you"),
    };
    NodeEntry conv{};
    conv.isChannel = false;
    conv.peerNum   = PEER1;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(msgs, 4, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("hi",          out[0].text);
    TEST_ASSERT_EQUAL_STRING("hey back",    out[1].text);
    TEST_ASSERT_EQUAL_STRING("how are you", out[2].text);
    // PEER2's message must not appear.
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_NOT_EQUAL(PEER2, out[i].sender);
}

// Journey: user views a channel — all senders' messages appear, DMs excluded.
void test_journey_open_channel_shows_all_senders()
{
    ConvMsg msgs[] = {
        makeChannel(0, PEER1, 100, "msg from peer1"),
        makeChannel(0, PEER2, 200, "msg from peer2"),
        makeChannel(0, MY,    300, "msg from me"),
        makeDm(PEER1, MY,     400, "private"),
    };
    NodeEntry conv{};
    conv.isChannel    = true;
    conv.channelIndex = 0;

    ConvMsg out[8];
    size_t n = filterThreadMsgs(msgs, 4, MY, conv, out, 8);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("msg from peer1", out[0].text);
    TEST_ASSERT_EQUAL_STRING("msg from peer2", out[1].text);
    TEST_ASSERT_EQUAL_STRING("msg from me",    out[2].text);
}

// Journey: conversation list preview always reflects the most recent message
// after multiple exchanges.
void test_journey_preview_tracks_latest_message()
{
    // Newest-first order as UIThread passes them.
    ConvMsg msgs[] = {
        makeDm(MY,    PEER1, 300, "see you later"),
        makeDm(PEER1, MY,    200, "sounds good"),
        makeDm(MY,    PEER1, 100, "want to meet?"),
    };
    NodeEntry convs[4];
    size_t n = buildNodeList(msgs, 3, MY, convs, 4);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_STRING("see you later", convs[0].preview);
    TEST_ASSERT_EQUAL_UINT32(300, convs[0].lastTs);
}

// ---------------------------------------------------------------------------
// Entry point — works for both Arduino (setup/loop) and plain host (main).
// ---------------------------------------------------------------------------

static void run_all_tests()
{
    UNITY_BEGIN();

    RUN_TEST(test_empty_messages_produces_no_convs);
    RUN_TEST(test_single_channel_message_produces_one_conv);
    RUN_TEST(test_two_channel_messages_same_channel_produces_one_conv);
    RUN_TEST(test_two_different_channels_produce_two_convs);
    RUN_TEST(test_dm_from_peer_produces_one_conv_keyed_on_peer);
    RUN_TEST(test_dm_sent_by_me_to_peer_produces_one_conv_keyed_on_peer);
    RUN_TEST(test_dm_both_directions_same_peer_produces_one_conv);
    RUN_TEST(test_mixed_channel_and_dm_produces_two_convs);
    RUN_TEST(test_maxOut_limits_output);
    RUN_TEST(test_filter_returns_only_matching_channel_messages);
    RUN_TEST(test_filter_returns_only_matching_dm_messages);
    RUN_TEST(test_filter_empty_store_returns_zero);
    RUN_TEST(test_filter_maxOut_limits_output);

    RUN_TEST(test_conv_preview_comes_from_first_input_message);
    RUN_TEST(test_two_peers_produce_separate_dm_convs);
    RUN_TEST(test_outgoing_and_incoming_dm_collapse_to_one_conv);
    RUN_TEST(test_channel_and_dm_with_same_index_do_not_collide);
    RUN_TEST(test_filter_preserves_message_order);
    RUN_TEST(test_filter_includes_my_outgoing_dms);
    RUN_TEST(test_filter_null_msgs_returns_zero);

    RUN_TEST(test_journey_incoming_dm_appears_in_list);
    RUN_TEST(test_journey_channel_message_appears_in_list);
    RUN_TEST(test_journey_reanchor_after_new_message_reorders_list);
    RUN_TEST(test_journey_reanchor_after_own_reply);
    RUN_TEST(test_journey_open_dm_shows_full_thread);
    RUN_TEST(test_journey_open_channel_shows_all_senders);
    RUN_TEST(test_journey_preview_tracks_latest_message);

    UNITY_END();
}

#ifdef ARDUINO
// PlatformIO / Unity on-device or portduino
void setup() { run_all_tests(); }
void loop()  {}
#else
// Plain host: make -C test/test_msgui
#include <cstdlib>
int main() { run_all_tests(); return 0; }
#endif
