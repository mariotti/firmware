// Unit tests for msgui::UIState (pure navigation state machine).
//
// Run directly (macOS / Linux, no PlatformIO):
//   make -C test/test_msgui test_state
//
// Tests cover: scrolling, tap-to-select, two-tap-to-open, scroll suppression,
// compose mode, send requests, sleep/wake, and the re-anchor invariant.

#include "graphics/msgui/UIState.h"
#include <cstring>
#include <unity.h>

using namespace msgui;

void setUp()   {}
void tearDown() {}

static constexpr uint32_t PEER1 = 0xBBBB0001;
static constexpr uint32_t PEER2 = 0xBBBB0002;

static NodeEntry makeConv(bool isChannel, uint8_t ch, uint32_t peer)
{
    NodeEntry e{};
    e.isChannel    = isChannel;
    e.channelIndex = ch;
    e.peerNum      = peer;
    snprintf(e.title, sizeof(e.title), isChannel ? "Ch%u" : "N%04x",
             (unsigned)(isChannel ? ch : (peer & 0xffff)));
    return e;
}

static void loadNConvs(UIState &s, size_t n)
{
    static NodeEntry buf[UIState::MAX_NODES];
    for (size_t i = 0; i < n && i < UIState::MAX_NODES; i++)
        buf[i] = makeConv(true, (uint8_t)i, 0);
    s.applyNodeList(buf, n);
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

void test_state_initial_view_is_conv_list()
{
    UIState s;
    s.init(0);
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
}

void test_state_initial_needs_render()
{
    UIState s;
    s.init(0);
    TEST_ASSERT_TRUE(s.needsRender());
}

void test_state_not_sleeping_initially()
{
    UIState s;
    s.init(0);
    TEST_ASSERT_FALSE(s.isSleeping());
}

// ---------------------------------------------------------------------------
// applyNodeList
// ---------------------------------------------------------------------------

void test_state_apply_conv_list_stores_count()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    TEST_ASSERT_EQUAL_size_t(3, s.nodeCount());
}

void test_state_apply_conv_list_reanchor_preserves_selected_peer()
{
    // This test encodes the invariant that exposed the re-anchor bug:
    // when the conversation list reorders after a new message, the
    // previously selected peer must remain selected.
    UIState s;
    s.init(0);

    NodeEntry list1[2];
    list1[0] = makeConv(false, 0, PEER1);
    s.applyNodeList(list1, 1);
    TEST_ASSERT_EQUAL_size_t(0, s.selectedNode()); // PEER1 at index 0

    // PEER2 arrives, list reorders: PEER2 first, PEER1 second.
    NodeEntry list2[2];
    list2[0] = makeConv(false, 0, PEER2);
    list2[1] = makeConv(false, 0, PEER1);
    s.applyNodeList(list2, 2);

    // selectedNode must still point to PEER1 (now at index 1).
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode());
    TEST_ASSERT_EQUAL_UINT32(PEER1, s.nodeList()[s.selectedNode()].peerNum);
}

void test_state_apply_conv_list_reanchor_preserves_channel()
{
    UIState s;
    s.init(0);

    NodeEntry list1[2];
    list1[0] = makeConv(true, 0, 0); // ch0
    s.applyNodeList(list1, 1);
    TEST_ASSERT_EQUAL_size_t(0, s.selectedNode()); // ch0 at index 0

    // A peer DM arrives and gets prepended.
    NodeEntry list2[2];
    list2[0] = makeConv(false, 0, PEER1);
    list2[1] = makeConv(true,  0, 0);
    s.applyNodeList(list2, 2);

    // Still on ch0, now at index 1.
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode());
    TEST_ASSERT_TRUE(s.nodeList()[s.selectedNode()].isChannel);
    TEST_ASSERT_EQUAL_UINT8(0, s.nodeList()[s.selectedNode()].channelIndex);
}

// ---------------------------------------------------------------------------
// Scrolling (CONV_LIST)
// ---------------------------------------------------------------------------

void test_state_up_increases_scroll_when_scrollable()
{
    UIState s;
    s.init(0);
    loadNConvs(s, VISIBLE_NODES + 3);
    TEST_ASSERT_EQUAL_size_t(0, s.nodeScroll());
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    TEST_ASSERT_GREATER_THAN_size_t(0, s.nodeScroll());
}

void test_state_down_decreases_scroll()
{
    UIState s;
    s.init(0);
    loadNConvs(s, VISIBLE_NODES + 3);
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    size_t afterUp = s.nodeScroll();
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 2000);
    TEST_ASSERT_LESS_THAN_size_t(afterUp, s.nodeScroll());
}

void test_state_up_does_not_scroll_when_no_room()
{
    UIState s;
    s.init(0);
    loadNConvs(s, VISIBLE_NODES); // exactly one page
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_size_t(0, s.nodeScroll());
}

void test_state_down_does_not_go_below_zero()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000); // already at top
    TEST_ASSERT_EQUAL_size_t(0, s.nodeScroll());
}

// ---------------------------------------------------------------------------
// USER_PRESS (tap to select / two-tap to open)
// ---------------------------------------------------------------------------

void test_state_user_press_selects_different_row()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    // Row 1: touchY within [CONTENT_Y + NODE_ROW_H, CONTENT_Y + 2*NODE_ROW_H)
    uint16_t y = (uint16_t)(CONTENT_Y + NODE_ROW_H + 5);
    s.handleInput(NavEvent::USER_PRESS, 0, y, 0, 500); // t >= SCROLL_SUPPRESS_MS
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode());
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView()); // not opened
}

void test_state_user_press_twice_opens_conv()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    uint16_t y = (uint16_t)(CONTENT_Y + NODE_ROW_H + 5); // row 1
    // First tap: select row 1 (selectedNode starts at 0, so row 1 != selected)
    s.handleInput(NavEvent::USER_PRESS, 0, y, 0, 500);
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode());
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
    // Second tap: row == selectedNode → open
    s.handleInput(NavEvent::USER_PRESS, 0, y, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
}

void test_state_scroll_suppresses_user_press()
{
    UIState s;
    s.init(0);
    loadNConvs(s, VISIBLE_NODES + 3);

    // First select row 1 (long before any scroll).
    uint16_t y1 = (uint16_t)(CONTENT_Y + NODE_ROW_H + 5);
    s.handleInput(NavEvent::USER_PRESS, 0, y1, 0, 500);
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode());

    // Scroll at t=1000.
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    size_t convScrollAfterUp = s.nodeScroll(); // > 0

    // USER_PRESS within suppress window (t=1000 + 399 = 1399) — should do nothing.
    // After scroll, visible row 0 = convScrollAfterUp + 0 (which != selectedNode=1 if scroll > 1).
    uint16_t y0 = (uint16_t)(CONTENT_Y + 5); // visible row 0
    s.handleInput(NavEvent::USER_PRESS, 0, y0, 0, 1399);
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode()); // unchanged

    // After suppress window (t=1400), the same tap should go through.
    s.handleInput(NavEvent::USER_PRESS, 0, y0, 0, 1400);
    size_t expectedRow = convScrollAfterUp + 0;
    TEST_ASSERT_EQUAL_size_t(expectedRow, s.selectedNode());
}

// ---------------------------------------------------------------------------
// SELECT (keyboard Enter / long-press)
// ---------------------------------------------------------------------------

void test_state_select_keyboard_opens_selected_conv()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    // touchY=0 < CONTENT_Y → keyboard Enter branch → opens selectedNode (0)
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
    TEST_ASSERT_EQUAL_size_t(0, s.selectedNode());
}

void test_state_select_touch_opens_row_by_y()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    // Row 2: touchY in [CONTENT_Y + 2*NODE_ROW_H, CONTENT_Y + 3*NODE_ROW_H)
    uint16_t y = (uint16_t)(CONTENT_Y + 2 * NODE_ROW_H + 5);
    s.handleInput(NavEvent::SELECT, 0, y, 0, 500);
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
    TEST_ASSERT_EQUAL_size_t(2, s.selectedNode());
}

void test_state_open_conv_sets_needs_conv_load()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.clearNeedsRender();
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500); // keyboard opens conv 0
    TEST_ASSERT_TRUE(s.needsThreadLoad());
}

// ---------------------------------------------------------------------------
// Navigation within CONV_VIEW
// ---------------------------------------------------------------------------

void test_state_back_from_conv_view_returns_to_list()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
    s.handleInput(NavEvent::BACK, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
}

void test_state_select_in_conv_view_opens_compose()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500); // open conv
    TEST_ASSERT_TRUE(s.isComposing()); // composing is always active in MSG_THREAD
}

// ---------------------------------------------------------------------------
// Compose mode
// ---------------------------------------------------------------------------

void test_state_anykey_appends_char()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);  // open
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000); // compose
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'h', 1500);
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'i', 2000);
    TEST_ASSERT_EQUAL_size_t(2, s.composeLen());
    TEST_ASSERT_EQUAL_STRING("hi", s.composeBuf());
}

void test_state_back_in_compose_deletes_last_char()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000);
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'a', 1500);
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'b', 2000);
    s.handleInput(NavEvent::BACK, 0, 0, 0,   2500);
    TEST_ASSERT_EQUAL_size_t(1, s.composeLen());
    TEST_ASSERT_EQUAL_STRING("a", s.composeBuf());
    TEST_ASSERT_TRUE(s.isComposing()); // still composing
}

void test_state_back_on_empty_compose_cancels()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    // Buffer is empty; BACK should return to node list.
    s.handleInput(NavEvent::BACK, 0, 0, 0, 1500);
    TEST_ASSERT_FALSE(s.isComposing());
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
}

void test_state_cancel_in_compose_clears_and_closes()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000);
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'x', 1500);
    s.handleInput(NavEvent::CANCEL, 0, 0, 0,   2000);
    TEST_ASSERT_FALSE(s.isComposing());
    TEST_ASSERT_EQUAL_size_t(0, s.composeLen());
}

void test_state_select_in_compose_sets_send_request()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000);
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'h', 1500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0,   2000); // send
    TEST_ASSERT_TRUE(s.hasSendRequest());
    TEST_ASSERT_EQUAL_STRING("h", s.composeBuf()); // still readable until onSendComplete
}

void test_state_select_in_empty_compose_does_not_send()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1500); // SELECT on empty buf
    TEST_ASSERT_FALSE(s.hasSendRequest());
}

void test_state_on_send_complete_resets_compose()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000);
    s.handleInput(NavEvent::ANYKEY, 0, 0, 'z', 1500);
    s.handleInput(NavEvent::SELECT, 0, 0, 0,   2000);
    TEST_ASSERT_TRUE(s.hasSendRequest());
    s.onSendComplete();
    TEST_ASSERT_FALSE(s.hasSendRequest());
    TEST_ASSERT_EQUAL_size_t(0, s.composeLen());
    TEST_ASSERT_EQUAL_size_t(0, s.msgScroll());
}

// ---------------------------------------------------------------------------
// Sleep / wake
// ---------------------------------------------------------------------------

void test_state_should_sleep_after_timeout()
{
    UIState s;
    s.init(0);
    TEST_ASSERT_FALSE(s.shouldSleep(UIState::SLEEP_TIMEOUT_MS - 1));
    TEST_ASSERT_TRUE(s.shouldSleep(UIState::SLEEP_TIMEOUT_MS));
}

void test_state_go_sleep_marks_sleeping()
{
    UIState s;
    s.init(0);
    s.goSleep();
    TEST_ASSERT_TRUE(s.isSleeping());
}

void test_state_any_input_wakes_from_sleep()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.goSleep();
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    TEST_ASSERT_FALSE(s.isSleeping());
    TEST_ASSERT_TRUE(s.needsRender());
    // The UP should not have navigated — wake consumes the event.
    TEST_ASSERT_EQUAL_size_t(0, s.nodeScroll());
}

void test_state_message_received_wakes_from_sleep()
{
    UIState s;
    s.init(0);
    s.goSleep();
    s.onMessageReceived(1000);
    TEST_ASSERT_FALSE(s.isSleeping());
}

void test_state_message_received_sets_rebuild_flag()
{
    UIState s;
    s.init(0);
    s.clearNeedsRender();
    s.onMessageReceived(1000);
    TEST_ASSERT_TRUE(s.needsListRebuild());
    TEST_ASSERT_TRUE(s.needsRender());
}

void test_state_message_received_in_conv_view_also_sets_conv_load()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 2);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500); // open conv 0
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
    s.clearNeedsRender();
    s.onMessageReceived(1000);
    TEST_ASSERT_TRUE(s.needsThreadLoad());
}

// ---------------------------------------------------------------------------
// onRenderDone / cooldown
// ---------------------------------------------------------------------------

void test_state_cooldown_suppresses_user_press()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.onRenderDone(500); // cooldown ends at 500 + RENDER_COOLDOWN_MS = 1500
    // USER_PRESS at t=1000 is within cooldown → ignored.
    uint16_t y = (uint16_t)(CONTENT_Y + NODE_ROW_H + 5); // row 1
    s.handleInput(NavEvent::USER_PRESS, 0, y, 0, 1000);
    TEST_ASSERT_EQUAL_size_t(0, s.selectedNode()); // unchanged
    // USER_PRESS at t=1500 (cooldown boundary, not yet past) — still blocked.
    // t=1501 should go through.
    s.handleInput(NavEvent::USER_PRESS, 0, y, 0, 1501);
    TEST_ASSERT_EQUAL_size_t(1, s.selectedNode()); // now selected
}

// ---------------------------------------------------------------------------
// Left/right navigation (screen cycling)
// ---------------------------------------------------------------------------

void test_state_swipe_left_opens_system_menu()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 2);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    TEST_ASSERT_EQUAL_INT((int)View::SETTINGS, (int)s.currentView());
    TEST_ASSERT_EQUAL_size_t(0, s.menuSelection()); // always resets to first button
}

void test_state_swipe_right_opens_system_menu()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 2);
    s.handleInput(NavEvent::RIGHT, 0, 0, 0, 500);
    TEST_ASSERT_EQUAL_INT((int)View::SETTINGS, (int)s.currentView());
}

void test_state_swipe_from_system_menu_returns_to_list()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 2);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    TEST_ASSERT_EQUAL_INT((int)View::SETTINGS, (int)s.currentView());
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
}

void test_state_back_from_system_menu_returns_to_list()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 2);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    s.handleInput(NavEvent::BACK, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
}

void test_state_swipe_from_conv_view_returns_to_list()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 2);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500); // open conv
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)View::NODE_LIST, (int)s.currentView());
}

// ---------------------------------------------------------------------------
// System menu navigation and activation
// ---------------------------------------------------------------------------

void test_state_system_menu_down_moves_selection()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    TEST_ASSERT_EQUAL_size_t(0, s.menuSelection());
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_size_t(1, s.menuSelection());
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1500);
    TEST_ASSERT_EQUAL_size_t(2, s.menuSelection());
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 2000);
    TEST_ASSERT_EQUAL_size_t(3, s.menuSelection());
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 2500);
    TEST_ASSERT_EQUAL_size_t(4, s.menuSelection());
    // At last item (SYS_MENU_ITEMS-1=4) — DOWN should not go further
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 3000);
    TEST_ASSERT_EQUAL_size_t(4, s.menuSelection());
}

void test_state_system_menu_up_moves_selection()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1500);
    TEST_ASSERT_EQUAL_size_t(2, s.menuSelection());
    s.handleInput(NavEvent::UP, 0, 0, 0, 2000);
    TEST_ASSERT_EQUAL_size_t(1, s.menuSelection());
    // At first item — UP should not go further
    s.handleInput(NavEvent::UP, 0, 0, 0, 2500);
    s.handleInput(NavEvent::UP, 0, 0, 0, 3000);
    TEST_ASSERT_EQUAL_size_t(0, s.menuSelection());
}

void test_state_system_menu_select_sleep()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);   // open menu, Sleep selected (0)
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)PowerAction::SLEEP, (int)s.pendingPowerAction());
}

void test_state_system_menu_select_restart()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000);  // Restart (1)
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 1500);
    TEST_ASSERT_EQUAL_INT((int)PowerAction::RESTART, (int)s.pendingPowerAction());
}

void test_state_system_menu_select_shutdown()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000);  // Restart
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1500);  // Shutdown (2)
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 2000);
    TEST_ASSERT_EQUAL_INT((int)PowerAction::SHUTDOWN, (int)s.pendingPowerAction());
}

void test_state_system_menu_user_press_activates()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500); // open menu
    // Touch Restart button: grid col=1 (x in [SYS_CELL_W, 2*SYS_CELL_W)), row=0
    uint16_t x = (uint16_t)(SYS_CELL_W + SYS_CELL_W / 2); // col 1, centre
    uint16_t y = (uint16_t)(CONTENT_Y + VIEW_HEADER_H + 10); // row 0
    s.handleInput(NavEvent::USER_PRESS, x, y, 0, 1000);
    TEST_ASSERT_EQUAL_INT((int)PowerAction::RESTART, (int)s.pendingPowerAction());
}

void test_state_system_menu_resets_selection_on_entry()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    // Enter menu, move to Shutdown, leave, re-enter — selection must reset.
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 500);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1500);
    TEST_ASSERT_EQUAL_size_t(2, s.menuSelection());
    s.handleInput(NavEvent::BACK, 0, 0, 0, 2000);
    s.handleInput(NavEvent::LEFT, 0, 0, 0, 2500);
    TEST_ASSERT_EQUAL_size_t(0, s.menuSelection()); // back to Sleep
}

// ---------------------------------------------------------------------------
// Message scrolling in Conversation View
// ---------------------------------------------------------------------------

void test_state_msg_scroll_up_moves_toward_older()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500); // open conv
    // Feed in enough messages to scroll
    ConvMsg msgs[5]{};
    s.applyThreadMsgs(msgs, 5);
    TEST_ASSERT_EQUAL_size_t(0, s.msgScroll());
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_size_t(1, s.msgScroll());
}

void test_state_msg_scroll_down_moves_toward_newer()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    ConvMsg msgs[5]{};
    s.applyThreadMsgs(msgs, 5);
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    s.handleInput(NavEvent::UP, 0, 0, 0, 1500);
    TEST_ASSERT_EQUAL_size_t(2, s.msgScroll());
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 2000);
    TEST_ASSERT_EQUAL_size_t(1, s.msgScroll());
}

void test_state_msg_scroll_does_not_go_below_zero()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    ConvMsg msgs[3]{};
    s.applyThreadMsgs(msgs, 3);
    s.handleInput(NavEvent::DOWN, 0, 0, 0, 1000);
    TEST_ASSERT_EQUAL_size_t(0, s.msgScroll());
}

void test_state_msg_scroll_does_not_exceed_msg_count()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    ConvMsg msgs[3]{};
    s.applyThreadMsgs(msgs, 3);
    // Max scroll is msgCount - 1 = 2
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    s.handleInput(NavEvent::UP, 0, 0, 0, 1500);
    s.handleInput(NavEvent::UP, 0, 0, 0, 2000); // should clamp
    TEST_ASSERT_EQUAL_size_t(2, s.msgScroll());
}

void test_state_send_resets_msg_scroll_to_bottom()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 1);
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500);
    ConvMsg msgs[4]{};
    s.applyThreadMsgs(msgs, 4);
    s.handleInput(NavEvent::UP, 0, 0, 0, 1000);
    s.handleInput(NavEvent::UP, 0, 0, 0, 1500);
    TEST_ASSERT_GREATER_THAN_size_t(0, s.msgScroll());
    s.onSendComplete();
    TEST_ASSERT_EQUAL_size_t(0, s.msgScroll()); // back to newest
}

// ---------------------------------------------------------------------------
// Unread notification tracking
// ---------------------------------------------------------------------------

void test_state_unread_count_zero_initially()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    TEST_ASSERT_EQUAL_size_t(0, s.unreadCount());
}

void test_state_mark_conv_unread_increments_count()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.markNodeUnread(0);
    TEST_ASSERT_EQUAL_size_t(1, s.unreadCount());
    s.markNodeUnread(2);
    TEST_ASSERT_EQUAL_size_t(2, s.unreadCount());
}

void test_state_open_conv_clears_unread()
{
    UIState s;
    s.init(0);
    loadNConvs(s, 3);
    s.markNodeUnread(0); // selectedNode starts at 0
    TEST_ASSERT_EQUAL_size_t(1, s.unreadCount());
    s.handleInput(NavEvent::SELECT, 0, 0, 0, 500); // keyboard Enter opens conv 0
    TEST_ASSERT_EQUAL_INT((int)View::MSG_THREAD, (int)s.currentView());
    TEST_ASSERT_EQUAL_size_t(0, s.unreadCount());
}

void test_state_unread_migrated_across_rebuild()
{
    UIState s;
    s.init(0);
    NodeEntry list1[2];
    list1[0] = makeConv(true, 0, 0);
    list1[1] = makeConv(true, 1, 0);
    s.applyNodeList(list1, 2);
    s.markNodeUnread(1); // channel 1 at index 1 has unread

    // Rebuild with reversed order — channel 1 is now at index 0
    NodeEntry list2[2];
    list2[0] = makeConv(true, 1, 0);
    list2[1] = makeConv(true, 0, 0);
    s.applyNodeList(list2, 2);

    TEST_ASSERT_EQUAL_size_t(1, s.unreadCount()); // flag followed channel 1 to its new index
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

static void run_state_tests()
{
    UNITY_BEGIN();

    RUN_TEST(test_state_initial_view_is_conv_list);
    RUN_TEST(test_state_initial_needs_render);
    RUN_TEST(test_state_not_sleeping_initially);

    RUN_TEST(test_state_apply_conv_list_stores_count);
    RUN_TEST(test_state_apply_conv_list_reanchor_preserves_selected_peer);
    RUN_TEST(test_state_apply_conv_list_reanchor_preserves_channel);

    RUN_TEST(test_state_up_increases_scroll_when_scrollable);
    RUN_TEST(test_state_down_decreases_scroll);
    RUN_TEST(test_state_up_does_not_scroll_when_no_room);
    RUN_TEST(test_state_down_does_not_go_below_zero);

    RUN_TEST(test_state_user_press_selects_different_row);
    RUN_TEST(test_state_user_press_twice_opens_conv);
    RUN_TEST(test_state_scroll_suppresses_user_press);

    RUN_TEST(test_state_select_keyboard_opens_selected_conv);
    RUN_TEST(test_state_select_touch_opens_row_by_y);
    RUN_TEST(test_state_open_conv_sets_needs_conv_load);

    RUN_TEST(test_state_back_from_conv_view_returns_to_list);
    RUN_TEST(test_state_select_in_conv_view_opens_compose);

    RUN_TEST(test_state_anykey_appends_char);
    RUN_TEST(test_state_back_in_compose_deletes_last_char);
    RUN_TEST(test_state_back_on_empty_compose_cancels);
    RUN_TEST(test_state_cancel_in_compose_clears_and_closes);
    RUN_TEST(test_state_select_in_compose_sets_send_request);
    RUN_TEST(test_state_select_in_empty_compose_does_not_send);
    RUN_TEST(test_state_on_send_complete_resets_compose);

    RUN_TEST(test_state_should_sleep_after_timeout);
    RUN_TEST(test_state_go_sleep_marks_sleeping);
    RUN_TEST(test_state_any_input_wakes_from_sleep);
    RUN_TEST(test_state_message_received_wakes_from_sleep);
    RUN_TEST(test_state_message_received_sets_rebuild_flag);
    RUN_TEST(test_state_message_received_in_conv_view_also_sets_conv_load);

    RUN_TEST(test_state_cooldown_suppresses_user_press);

    RUN_TEST(test_state_swipe_left_opens_system_menu);
    RUN_TEST(test_state_swipe_right_opens_system_menu);
    RUN_TEST(test_state_swipe_from_system_menu_returns_to_list);
    RUN_TEST(test_state_back_from_system_menu_returns_to_list);
    RUN_TEST(test_state_swipe_from_conv_view_returns_to_list);

    RUN_TEST(test_state_system_menu_down_moves_selection);
    RUN_TEST(test_state_system_menu_up_moves_selection);
    RUN_TEST(test_state_system_menu_select_sleep);
    RUN_TEST(test_state_system_menu_select_restart);
    RUN_TEST(test_state_system_menu_select_shutdown);
    RUN_TEST(test_state_system_menu_user_press_activates);
    RUN_TEST(test_state_system_menu_resets_selection_on_entry);

    RUN_TEST(test_state_msg_scroll_up_moves_toward_older);
    RUN_TEST(test_state_msg_scroll_down_moves_toward_newer);
    RUN_TEST(test_state_msg_scroll_does_not_go_below_zero);
    RUN_TEST(test_state_msg_scroll_does_not_exceed_msg_count);
    RUN_TEST(test_state_send_resets_msg_scroll_to_bottom);

    RUN_TEST(test_state_unread_count_zero_initially);
    RUN_TEST(test_state_mark_conv_unread_increments_count);
    RUN_TEST(test_state_open_conv_clears_unread);
    RUN_TEST(test_state_unread_migrated_across_rebuild);

    UNITY_END();
}

#ifdef ARDUINO
void setup() { run_state_tests(); }
void loop()  {}
#else
#include <cstdlib>
int main() { run_state_tests(); return 0; }
#endif
