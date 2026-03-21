#include "StyleEngine.h"
#include "Colors.h"

namespace StyleEngine {

QString globalStyle() {
    return QStringLiteral(R"(

/* ── Global canvas & root containers ── */
QMainWindow, QWidget#appRoot, QWidget#bodyShell, QStackedWidget#pageStack,
QWidget#pageCanvas, QWidget#pageViewport, QScrollArea#pageScroll, QDialog#deepDiveDialog {
    background: #0d1117;
    color: #e6edf3;
    font-family: 'Segoe UI', 'Inter', 'Helvetica Neue', sans-serif;
}

QWidget#appRoot {
    background: qlineargradient(x1:0, y1:0, x2:0.6, y2:1,
        stop:0 #080c14, stop:0.4 #0d1117, stop:0.75 #0b1018, stop:1 #080c14);
}

QWidget#bodyShell, QStackedWidget#pageStack {
    background: #0d1117;
}

QScrollArea#pageScroll {
    border: none;
}

QWidget#pageCanvas {
    background: #0d1117;
}

QDialog#deepDiveDialog {
    background: qlineargradient(x1:0, y1:0, x2:0.6, y2:1,
        stop:0 #0b1018, stop:0.55 #0d1117, stop:1 #080c14);
}

/* ── Tooltips ── */
QToolTip {
    background: #161b22;
    border: 1px solid #2a3a4e;
    border-radius: 8px;
    color: #e6edf3;
    font-size: 12px;
    padding: 6px 10px;
}

/* ── Mode indicator ── */
QComboBox#modeSelect {
    font-weight: 600;
}

    )");
}

QString chromeBarStyle() {
    return QStringLiteral(R"(

/* ── Title bar / chrome ── */
QWidget#chromeBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 rgba(8, 14, 26, 0.98), stop:1 rgba(8, 12, 20, 0.96));
    border-bottom: 1px solid #1e2d3d;
}

QLabel#chromeTitle {
    color: #e6edf3;
    font-size: 13px;
    font-weight: 600;
    letter-spacing: 0.3px;
}

QPushButton#sidebarToggle {
    background: rgba(13, 17, 23, 0.85);
    border: 1px solid #1e2d3d;
    border-radius: 8px;
    color: #8b949e;
    font-size: 13px;
    min-width: 32px;
    min-height: 32px;
}

QPushButton#sidebarToggle:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1c2333, stop:1 #161b22);
    border-color: #2a3a4e;
    color: #e6edf3;
}

QPushButton#chromeMin, QPushButton#chromeMax, QPushButton#chromeClose {
    background: rgba(13, 17, 23, 0.85);
    border: 1px solid #1e2d3d;
    border-radius: 8px;
    color: #8b949e;
    font-size: 13px;
    min-width: 32px;
    min-height: 26px;
}

QPushButton#chromeMin:hover, QPushButton#chromeMax:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1c2333, stop:1 #161b22);
    border-color: #2a3a4e;
    color: #e6edf3;
}

QPushButton#chromeClose:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #ef4444, stop:1 #dc2626);
    border-color: #ef4444;
    color: #ffffff;
}

    )");
}

QString sidebarStyle() {
    return QStringLiteral(R"(

/* ── Sidebar ── */
QFrame#sidebar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #0a1018, stop:0.5 #0d1117, stop:1 #0a1018);
    border-right: 1px solid #1e2d3d;
}

QWidget#brandBadge {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #161b22, stop:0.5 #131920, stop:1 #0d1117);
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 14px;
}

QLabel#brandIcon {
    font-size: 20px;
}

QLabel#brandStatusLight[connected="true"] {
    color: #22c55e;
    font-size: 8px;
}

QLabel#brandStatusLight[connected="false"] {
    color: #ef4444;
    font-size: 8px;
}

QLabel#brandName {
    color: #e6edf3;
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 0.2px;
}

QLabel#brandVer {
    color: #5a6a7e;
    font-size: 11px;
}

QLabel#sectionLabel {
    color: #3d4f63;
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 2px;
}

/* ── Navigation buttons ── */
QPushButton#navButton {
    background: transparent;
    border: none;
    border-left: 3px solid transparent;
    border-radius: 0px;
    color: #8b949e;
    font-size: 13px;
    text-align: left;
    padding: 8px 12px;
}

QPushButton#navButton[collapsed="true"] {
    font-size: 16px;
    text-align: center;
    padding: 8px 0px;
}

QPushButton#navButton:hover {
    background: rgba(22, 27, 34, 0.85);
    color: #c7d1db;
    border-left: 3px solid rgba(63, 185, 80, 0.30);
}

QPushButton#navButton:checked {
    background: rgba(17, 50, 35, 0.80);
    border-left: 3px solid #3fb950;
    color: #3fb950;
    font-weight: 600;
}

QPushButton#navButton:checked:hover {
    color: #53d769;
    border-left: 3px solid #53d769;
}

QWidget#quickAccessRow {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 rgba(22, 27, 34, 0.65), stop:1 rgba(13, 17, 23, 0.35));
    border: 1px solid rgba(30, 45, 61, 0.85);
    border-radius: 12px;
}

QWidget#quickAccessRow:hover {
    border-color: rgba(59, 130, 246, 0.55);
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 rgba(28, 35, 51, 0.78), stop:1 rgba(13, 17, 23, 0.48));
}

QLabel#quickAccessLabel {
    color: #c7d1db;
    font-size: 12px;
    font-weight: 600;
}

/* ── Runtime info labels ── */
QLabel#runtimeKey {
    color: #8b949e;
    font-size: 13px;
}

QLabel#runtimeVal {
    color: #e6edf3;
    font-size: 13px;
    font-weight: 500;
}

    )");
}

QString cardStyle() {
    return QStringLiteral(R"(

/* ── Summary chips ── */
QWidget#summaryChip {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 rgba(22, 27, 34, 0.96), stop:1 rgba(13, 17, 23, 0.94));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 16px;
}

QWidget#heroSummaryCard {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0.3,
        stop:0 rgba(22, 27, 34, 0.98), stop:0.5 rgba(13, 17, 23, 0.98), stop:1 rgba(11, 16, 24, 0.98));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 20px;
    padding: 2px;
}

QWidget#heroSummaryField {
    background: transparent;
}

QWidget#heroSummaryDivider {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1e2d3d, stop:0.5 #2a3a4e, stop:1 #1e2d3d);
    min-height: 30px;
    max-height: 30px;
}

QLabel#summaryChipLabel {
    color: #8b949e;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.8px;
    text-transform: uppercase;
}

QLabel#summaryChipValue {
    color: #e6edf3;
    font-size: 15px;
    font-weight: 700;
}

/* ── Metric cards ── */
QWidget#metricCard {
    background: qlineargradient(x1:0, y1:0, x2:0.8, y2:1,
        stop:0 rgba(22, 27, 34, 0.98), stop:0.5 rgba(16, 21, 28, 0.97), stop:1 rgba(13, 17, 23, 0.96));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 16px;
    padding: 4px;
}

QWidget#metricCard:hover {
    background: qlineargradient(x1:0, y1:0, x2:0.8, y2:1,
        stop:0 rgba(28, 35, 51, 0.98), stop:0.5 rgba(22, 27, 34, 0.97), stop:1 rgba(16, 21, 28, 0.97));
    border: 1px solid #2a3a4e;
    border-top: 1px solid #3b82f6;
}

QLabel#metricLabel {
    color: #5a6a7e;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 2px;
}

QLabel#metricValue {
    color: #e6edf3;
    font-size: 34px;
    font-weight: 700;
    letter-spacing: -0.5px;
}

QWidget#miniInsightCard {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(22, 27, 34, 0.96), stop:0.55 rgba(13, 17, 23, 0.95), stop:1 rgba(8, 12, 20, 0.94));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 16px;
    padding: 2px;
}

QWidget#miniInsightCard:hover {
    border: 1px solid #2a3a4e;
    border-top: 1px solid #3b82f6;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(28, 35, 51, 0.98), stop:0.55 rgba(22, 27, 34, 0.97), stop:1 rgba(13, 17, 23, 0.96));
}

QLabel#insightValue {
    color: #e6edf3;
    font-size: 18px;
    font-weight: 700;
    letter-spacing: -0.2px;
}

/* ── Generic card, control card, system card ── */
QWidget#card, QWidget#controlCard, QWidget#systemCard {
    background: qlineargradient(x1:0, y1:0, x2:0.7, y2:1,
        stop:0 rgba(22, 27, 34, 0.97), stop:0.5 rgba(16, 21, 28, 0.97), stop:1 rgba(13, 17, 23, 0.97));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 16px;
    padding: 2px;
}

QWidget#card:hover, QWidget#controlCard:hover, QWidget#systemCard:hover {
    background: qlineargradient(x1:0, y1:0, x2:0.7, y2:1,
        stop:0 rgba(28, 35, 51, 0.98), stop:0.5 rgba(22, 27, 34, 0.98), stop:1 rgba(16, 21, 28, 0.98));
    border: 1px solid #2a3a4e;
    border-top: 1px solid #3b82f6;
}

QLabel#cardTitle {
    color: #8b949e;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1.4px;
    text-transform: uppercase;
}

QLabel#controlTitle {
    color: #e6edf3;
    font-size: 15px;
    font-weight: 700;
}

QLabel#controlDescription {
    color: #8b949e;
    font-size: 13px;
    line-height: 1.5;
}

QLabel#controlMeta {
    color: #5a6a7e;
    font-size: 12px;
    line-height: 1.5;
}

QWidget#controlActionRow {
    background: transparent;
    border: none;
}

/* ── Mission card ── */
QWidget#missionCard {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(24, 31, 41, 0.98), stop:0.55 rgba(15, 22, 31, 0.98), stop:1 rgba(10, 16, 24, 0.98));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 14px;
}

QWidget#missionDivider {
    background: #1e2d3d;
    min-height: 1px;
    max-height: 1px;
}

QLabel#missionCardLabel {
    color: #8b949e;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 2px;
}

QLabel#missionStatus {
    color: #3fb950;
    font-size: 13px;
    font-weight: 700;
}

QLabel#missionName {
    color: #e6edf3;
    font-size: 19px;
    font-weight: 700;
    letter-spacing: -0.2px;
}

QLabel#missionSummary {
    color: #c7d1db;
    font-size: 13px;
    line-height: 1.45;
}

QLabel#missionMetaLabel {
    color: #7d8590;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1.2px;
}

QLabel#missionMetaValue {
    color: #d2dae3;
    font-size: 13px;
    font-weight: 600;
}

QWidget#jobsFocusPanel {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(12, 20, 31, 0.82), stop:1 rgba(10, 16, 24, 0.82));
    border: 1px solid rgba(42, 58, 78, 0.9);
    border-radius: 12px;
}

QLabel#jobsFocusMeta {
    color: #8b949e;
    font-size: 12px;
    line-height: 1.45;
}

QLabel#missionFoot, QLabel#systemInfo {
    color: #8b949e;
    font-size: 13px;
    line-height: 1.5;
}

QLabel#autopilotMissionState {
    color: #c084fc;
    font-size: 11px;
    font-weight: 800;
    letter-spacing: 1.8px;
    text-transform: uppercase;
}

QLabel#autopilotMissionSummary {
    color: #e6edf3;
    font-size: 15px;
    font-weight: 700;
    line-height: 1.4;
}

QLabel#autopilotMissionMeta {
    color: #b6ceff;
    font-size: 12px;
    font-weight: 600;
    line-height: 1.4;
    padding: 7px 10px;
    border-radius: 10px;
    background: rgba(13, 24, 40, 0.48);
    border: 1px solid rgba(58, 101, 176, 0.2);
}

QLabel#autopilotMissionRecovery {
    color: #c8dcff;
    font-size: 12px;
    line-height: 1.45;
    padding: 7px 12px;
    border-radius: 10px;
    background: rgba(16, 30, 51, 0.56);
    border: 1px solid rgba(76, 122, 210, 0.24);
}

QLabel#autopilotMissionHint {
    color: #7d8590;
    font-size: 12px;
    line-height: 1.45;
}

QWidget#autopilotTimelineWrap {
    background: transparent;
    border: none;
}

QWidget#autopilotTimelineConnector {
    background: rgba(49, 63, 84, 0.9);
    border-radius: 2px;
}

QWidget#autopilotTimelineConnector[state="done"] {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 rgba(34, 197, 94, 0.8), stop:1 rgba(20, 184, 166, 0.78));
}

QWidget#autopilotTimelineConnector[state="active"] {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 rgba(59, 130, 246, 0.88), stop:1 rgba(168, 85, 247, 0.88));
}

QWidget#autopilotTimelineConnector[state="paused"] {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 rgba(245, 158, 11, 0.88), stop:1 rgba(234, 179, 8, 0.88));
}

QPushButton#autopilotStageButton {
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(16, 24, 36, 0.99), stop:0.55 rgba(11, 18, 28, 0.98), stop:1 rgba(9, 14, 22, 0.98));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #30445c;
    border-radius: 18px;
    padding: 0;
    text-align: left;
}

QPushButton#autopilotStageButton[clickable="true"]:hover {
    border-color: #58a6ff;
    border-top-color: #79c0ff;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(24, 36, 54, 0.99), stop:0.55 rgba(15, 24, 38, 0.99), stop:1 rgba(11, 18, 28, 0.98));
}

QPushButton#autopilotStageButton[state="done"] {
    border-color: #1f6d49;
    border-top-color: #2fbf71;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(14, 40, 28, 0.98), stop:1 rgba(11, 24, 19, 0.98));
}

QPushButton#autopilotStageButton[state="active"] {
    border-color: #3b82f6;
    border-top-color: #a855f7;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(18, 38, 72, 0.99), stop:0.55 rgba(29, 23, 63, 0.98), stop:1 rgba(14, 18, 34, 0.98));
}

QPushButton#autopilotStageButton[state="paused"] {
    border-color: #f59e0b;
    border-top-color: #facc15;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(58, 36, 11, 0.99), stop:1 rgba(28, 21, 10, 0.98));
}

QPushButton#autopilotStageButton[state="ready"] {
    border-color: #24558a;
    border-top-color: #60a5fa;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(16, 32, 58, 0.98), stop:1 rgba(10, 17, 28, 0.98));
}

QPushButton#autopilotStageButton[state="queued"] {
    border-color: #38485d;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(14, 20, 29, 0.97), stop:1 rgba(9, 13, 20, 0.97));
}

QPushButton#autopilotStageButton[state="failed"] {
    border-color: #f85149;
    border-top-color: #ff7b72;
    background: qlineargradient(x1:0, y1:0, x2:0.88, y2:1,
        stop:0 rgba(60, 17, 24, 0.99), stop:1 rgba(27, 10, 14, 0.98));
}

QPushButton#autopilotStageButton:disabled {
    color: #6e7681;
}

QLabel#autopilotStageStep,
QLabel#autopilotStageState,
QLabel#autopilotStageTitle,
QLabel#autopilotStageDetail,
QLabel#autopilotStageProgress {
    background: transparent;
}

QLabel#autopilotStageStep {
    color: #7d8590;
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 1.6px;
}

QLabel#autopilotStageTitle {
    color: #e6edf3;
    font-size: 16px;
    font-weight: 700;
}

QLabel#autopilotStageDetail {
    color: #aab9ca;
    font-size: 12px;
    line-height: 1.35;
}

QLabel#autopilotStageProgress {
    color: #c7d1db;
    font-size: 12px;
    font-weight: 600;
}

QLabel#autopilotStageState {
    padding: 4px 8px;
    border-radius: 10px;
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 1.2px;
    color: #8b949e;
    background: rgba(139, 148, 158, 0.12);
    border: 1px solid rgba(139, 148, 158, 0.35);
}

QLabel#autopilotStageState[state="done"] {
    color: #4ade80;
    background: rgba(34, 197, 94, 0.14);
    border-color: rgba(34, 197, 94, 0.45);
}

QLabel#autopilotStageState[state="active"] {
    color: #79c0ff;
    background: rgba(59, 130, 246, 0.16);
    border-color: rgba(96, 165, 250, 0.48);
}

QLabel#autopilotStageState[state="paused"] {
    color: #fbbf24;
    background: rgba(245, 158, 11, 0.16);
    border-color: rgba(245, 158, 11, 0.48);
}

QLabel#autopilotStageState[state="ready"] {
    color: #c4b5fd;
    background: rgba(124, 58, 237, 0.14);
    border-color: rgba(168, 85, 247, 0.42);
}

QLabel#autopilotStageState[state="queued"] {
    color: #94a3b8;
    background: rgba(71, 85, 105, 0.16);
    border-color: rgba(100, 116, 139, 0.35);
}

QLabel#autopilotStageState[state="failed"] {
    color: #ff7b72;
    background: rgba(248, 81, 73, 0.15);
    border-color: rgba(248, 81, 73, 0.46);
}

QWidget#notificationToast {
    background: qlineargradient(x1:0, y1:0, x2:0.9, y2:1,
        stop:0 rgba(22, 27, 34, 0.98), stop:0.55 rgba(16, 21, 28, 0.98), stop:1 rgba(13, 17, 23, 0.97));
    border: 1px solid #1e2d3d;
    border-left: 4px solid #58a6ff;
    border-radius: 14px;
}

QWidget#notificationToast[severity="error"] {
    border-left: 4px solid #f85149;
}

QWidget#notificationToast[severity="warning"] {
    border-left: 4px solid #e3b341;
}

QWidget#notificationToast[severity="info"] {
    border-left: 4px solid #58a6ff;
}

QLabel#notificationTitle {
    background: transparent;
    color: #e6edf3;
    font-size: 13px;
    font-weight: 700;
    letter-spacing: 0.3px;
}

QLabel#notificationMessage {
    background: transparent;
    color: #c7d1db;
    font-size: 12px;
    line-height: 1.45;
}

QWidget#systemRail {
    background: transparent;
}

/* ── Job cards ── */
QWidget#jobCard {
    background: qlineargradient(x1:0, y1:0, x2:0.7, y2:1,
        stop:0 rgba(22, 27, 34, 0.97), stop:1 rgba(13, 17, 23, 0.96));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 14px;
}

QWidget#jobCard:hover {
    background: qlineargradient(x1:0, y1:0, x2:0.7, y2:1,
        stop:0 rgba(28, 35, 51, 0.98), stop:1 rgba(22, 27, 34, 0.97));
    border: 1px solid #2a3a4e;
    border-top: 1px solid #3b82f6;
}

QLabel#jobLog {
    background: #080c14;
    border: 1px solid #1e2d3d;
    border-radius: 10px;
    color: #8b949e;
    font-family: 'Cascadia Code', 'Consolas', monospace;
    font-size: 11px;
    padding: 10px;
}

QLabel#jobsCardMessage {
    color: #d2dae3;
    font-size: 12px;
    line-height: 1.45;
    padding: 8px 10px;
    border-radius: 10px;
    background: rgba(15, 24, 37, 0.6);
    border: 1px solid rgba(43, 66, 97, 0.32);
}

QLabel#jobsCardMeta {
    color: #8b949e;
    font-size: 12px;
    line-height: 1.45;
}

QLabel#jobsCardNote {
    color: #c1cedb;
    font-size: 12px;
    line-height: 1.45;
    padding: 8px 10px;
    border-radius: 10px;
    background: rgba(11, 18, 28, 0.72);
    border: 1px solid rgba(30, 45, 61, 0.88);
}

QWidget#jobsActivityItem {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(13, 21, 32, 0.9), stop:1 rgba(10, 16, 24, 0.9));
    border: 1px solid #1e2d3d;
    border-radius: 12px;
}

QWidget#jobsActivityItem:hover {
    border-color: #2a3a4e;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(18, 29, 44, 0.94), stop:1 rgba(12, 19, 29, 0.92));
}

QLabel#jobsActivitySummary {
    color: #b9c7d6;
    font-size: 12px;
    line-height: 1.45;
}

QLabel#jobsActivityTitle {
    color: #e6edf3;
    font-size: 13px;
    font-weight: 600;
    line-height: 1.4;
}

QLabel#jobsActivityMeta {
    color: #8b949e;
    font-size: 12px;
    line-height: 1.4;
}

QLabel#jobsActivityStamp {
    color: #7d8590;
    font-size: 11px;
    font-weight: 600;
}

    )");
}

QString inputStyle() {
    return QStringLiteral(R"(

/* ── Form inputs ── */
QComboBox, QLineEdit, QPlainTextEdit, QTextEdit {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #0d1520, stop:1 #0d1117);
    border: 1px solid #1e2d3d;
    border-radius: 8px;
    color: #e6edf3;
    padding: 8px 12px;
    font-size: 13px;
    min-height: 18px;
    selection-background-color: rgba(63, 185, 80, 0.25);
    selection-color: #e6edf3;
}

QComboBox:focus, QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus {
    border: 1px solid #3b82f6;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #131920, stop:1 #0d1520);
}

QComboBox:hover, QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover {
    border-color: #2a3a4e;
}

QComboBox::drop-down {
    border: none;
    width: 28px;
}

QComboBox::down-arrow {
    image: none;
}

QComboBox QAbstractItemView {
    background: #161b22;
    border: 1px solid #2a3a4e;
    border-radius: 8px;
    color: #e6edf3;
    selection-background-color: rgba(63, 185, 80, 0.25);
    padding: 4px;
}

/* ── Settings labels ── */
QLabel#settingsLabel {
    color: #8b949e;
    font-size: 12px;
    font-weight: 600;
}

/* ── Editor / text areas ── */
QPlainTextEdit#yamlEditor, QPlainTextEdit#feedArea, QPlainTextEdit#reportArea, QTextEdit#chatMessages {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #080c14, stop:1 #0d1117);
    border: 1px solid #1e2d3d;
    border-radius: 14px;
    color: #e6edf3;
    font-family: 'Cascadia Code', 'Consolas', monospace;
    padding: 6px;
}

QPlainTextEdit#yamlEditor:focus, QPlainTextEdit#feedArea:focus,
QPlainTextEdit#reportArea:focus, QTextEdit#chatMessages:focus {
    border: 1px solid #3b82f6;
}

/* ── Chat input ── */
QLineEdit#chatInput {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #161b22, stop:1 #0d1520);
    border: 1px solid #1e2d3d;
    border-radius: 12px;
    color: #e6edf3;
    padding: 11px 14px;
    font-size: 13px;
}

QLineEdit#chatInput:focus {
    border: 1px solid #3b82f6;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1c2333, stop:1 #161b22);
}

    )");
}

QString buttonStyle() {
    return QStringLiteral(R"(

/* ── Action buttons ── */
QPushButton#actionBtn, QPushButton#actionBtnPrimary, QPushButton#actionBtnDanger {
    min-height: 44px;
    border-radius: 12px;
    font-size: 13px;
    font-weight: 700;
    padding: 0 18px;
}

QPushButton#actionBtn {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(19, 28, 40, 0.98), stop:1 rgba(13, 18, 28, 0.98));
    border: 1px solid #26415f;
    color: #79c0ff;
}

QPushButton#actionBtn:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(31, 47, 68, 0.98), stop:1 rgba(18, 29, 43, 0.98));
    border-color: #3b82f6;
    color: #c2e7ff;
}

QPushButton#actionBtn:pressed {
    background: rgba(20, 34, 49, 0.98);
    border-color: #3b82f6;
    color: #d7efff;
}

QPushButton#actionBtnPrimary {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(22, 72, 43, 0.98), stop:1 rgba(17, 54, 33, 0.98));
    border: 1px solid #2fbf71;
    color: #7ee2a8;
}

QPushButton#actionBtnPrimary:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(36, 111, 67, 0.98), stop:1 rgba(27, 84, 51, 0.98));
    border-color: #4ade80;
    color: #effff5;
}

QPushButton#actionBtnPrimary:pressed {
    background: rgba(28, 89, 54, 0.98);
    border-color: #4ade80;
    color: #effff5;
}

QPushButton#actionBtnDanger {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(62, 19, 27, 0.98), stop:1 rgba(42, 14, 20, 0.98));
    border: 1px solid #ff5f56;
    color: #ff8f88;
}

QPushButton#actionBtnDanger:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(102, 30, 42, 0.98), stop:1 rgba(74, 22, 31, 0.98));
    border-color: #ff7b72;
    color: #ffffff;
}

QPushButton#actionBtnDanger:pressed {
    background: rgba(78, 23, 33, 0.98);
    border-color: #ff7b72;
    color: #ffffff;
}

/* ── Time range toggle buttons ── */
/* Mission stop button */
QPushButton#missionStopBtn {
    min-height: 50px;
    max-height: 50px;
    border-radius: 12px;
    font-size: 14px;
    font-weight: 700;
    padding: 0 22px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #3b161d, stop:1 #2a1016);
    border: 2px solid #f85149;
    color: #ff7b72;
}

QPushButton#missionStopBtn:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #f85149, stop:1 #da3633);
    border-color: #f85149;
    color: #ffffff;
}

QPushButton#missionStopBtn:pressed {
    background: #da3633;
    border-color: #da3633;
    color: #ffffff;
}

QPushButton#rangeBtn {
    background: transparent;
    border: 1px solid #1e2d3d;
    border-radius: 4px;
    color: #8b949e;
    font-size: 11px;
    font-weight: 600;
    padding: 2px 6px;
    margin: 0 1px;
}

QPushButton#rangeBtn:hover {
    background: rgba(56, 139, 253, 0.10);
    border-color: #388bfd;
    color: #c9d1d9;
}

QPushButton#rangeBtn:checked {
    background: rgba(56, 139, 253, 0.20);
    border-color: #388bfd;
    color: #58a6ff;
}

    )");
}

QString progressBarStyle() {
    return QStringLiteral(R"(

/* ── Progress bars ── */
QProgressBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #0d1117, stop:1 #080c14);
    border: 1px solid #1e2d3d;
    border-radius: 7px;
    min-height: 14px;
    max-height: 14px;
    text-align: center;
    color: transparent;
}

QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #3b82f6, stop:0.5 #a855f7, stop:1 #ec4899);
    border-radius: 6px;
}

    )");
}

QString chartStyle() {
    return QStringLiteral(R"(

/* ── Chart cards ── */
QWidget#chartCard {
    background: qlineargradient(x1:0, y1:0, x2:0.85, y2:1,
        stop:0 rgba(22, 27, 34, 0.98), stop:0.5 rgba(13, 17, 23, 0.97), stop:1 rgba(8, 12, 20, 0.96));
    border: 1px solid #1e2d3d;
    border-top: 1px solid #2a3a4e;
    border-radius: 16px;
    padding: 3px;
}

QWidget#chartCard:hover {
    border: 1px solid #2a3a4e;
    border-top: 1px solid #3b82f6;
    background: qlineargradient(x1:0, y1:0, x2:0.85, y2:1,
        stop:0 rgba(28, 35, 51, 0.99), stop:0.5 rgba(22, 27, 34, 0.98), stop:1 rgba(13, 17, 23, 0.97));
}

    )");
}

QString alertStyle() {
    return QStringLiteral(R"(

/* ── Alert badges ── */
QLabel#alertHeaderCount {
    color: #22c55e;
    font-size: 11px;
    font-weight: 700;
    padding: 4px 10px;
    background: rgba(34, 197, 94, 0.12);
    border: 1px solid rgba(34, 197, 94, 0.45);
    border-radius: 10px;
}

QLabel#alertHeaderCount[severity="error"] {
    color: #f85149;
    background: rgba(248, 81, 73, 0.12);
    border-color: rgba(248, 81, 73, 0.5);
}

QLabel#alertHeaderCount[severity="warning"] {
    color: #e3b341;
    background: rgba(227, 179, 65, 0.12);
    border-color: rgba(227, 179, 65, 0.5);
}

QLabel#alertHeaderCount[severity="info"] {
    color: #58a6ff;
    background: rgba(88, 166, 255, 0.12);
    border-color: rgba(88, 166, 255, 0.45);
}

QLabel#alertHeaderCount[severity="clear"] {
    color: #22c55e;
    background: rgba(34, 197, 94, 0.12);
    border-color: rgba(34, 197, 94, 0.45);
}

/* ── Alert container & items ── */
QWidget#alertContainer {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #080c14, stop:1 #0d1117);
    border: 1px solid #1e2d3d;
    border-radius: 14px;
}

QLabel#alertError, QLabel#alertWarning, QLabel#alertInfo {
    font-family: 'Cascadia Code', 'Consolas', monospace;
    font-size: 12px;
    padding: 6px 10px;
    border-radius: 8px;
}

QLabel#alertError {
    color: #f85149;
    background: rgba(248, 81, 73, 0.12);
    border-left: 3px solid #f85149;
}

QLabel#alertWarning {
    color: #e3b341;
    background: rgba(227, 179, 65, 0.12);
    border-left: 3px solid #f59e0b;
}

QLabel#alertInfo {
    color: #58a6ff;
    background: rgba(56, 139, 253, 0.12);
    border-left: 3px solid #3b82f6;
}

/* —— Diagnostics surfaces —— */
QWidget#diagList {
    background: transparent;
}

QWidget#diagRow, QWidget#diagIssueRow {
    background: qlineargradient(x1:0, y1:0, x2:0.8, y2:1,
        stop:0 rgba(13, 17, 23, 0.98), stop:1 rgba(8, 12, 20, 0.97));
    border: 1px solid #1e2d3d;
    border-radius: 12px;
}

QWidget#diagIssueRow {
    border-color: #2a3a4e;
}

QLabel#diagBadge {
    border-radius: 9px;
    padding: 4px 8px;
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 1px;
}

QLabel#diagBadge[status="ok"] {
    background: rgba(34, 197, 94, 0.15);
    color: #3fb950;
    border: 1px solid rgba(63, 185, 80, 0.30);
}

QLabel#diagBadge[status="warning"] {
    background: rgba(227, 179, 65, 0.15);
    color: #e3b341;
    border: 1px solid rgba(227, 179, 65, 0.30);
}

QLabel#diagBadge[status="error"] {
    background: rgba(248, 81, 73, 0.15);
    color: #f85149;
    border: 1px solid rgba(248, 81, 73, 0.30);
}

QLabel#diagBadge[status="info"] {
    background: rgba(56, 139, 253, 0.15);
    color: #58a6ff;
    border: 1px solid rgba(88, 166, 255, 0.30);
}

QLabel#diagMeta {
    color: #5a6a7e;
    font-size: 11px;
}

QLabel#diagEmpty {
    color: #5a6a7e;
    font-size: 13px;
    font-style: italic;
    padding: 10px 2px;
}

QLabel#diagHelperText {
    color: #8b949e;
    font-size: 12px;
    line-height: 1.5;
}

QLabel#diagHelperText[severity="error"] {
    color: #f85149;
}

QLabel#diagHelperText[severity="info"] {
    color: #58a6ff;
}

/* —— Data lineage / history surfaces —— */
QLabel#historyPanelSummary {
    color: #8b949e;
    font-size: 12px;
    line-height: 1.5;
}

QWidget#historyRow {
    background: qlineargradient(x1:0, y1:0, x2:0.85, y2:1,
        stop:0 rgba(12, 18, 27, 0.98), stop:1 rgba(8, 12, 20, 0.97));
    border: 1px solid #1e2d3d;
    border-radius: 14px;
}

QWidget#historyRow:hover {
    background: qlineargradient(x1:0, y1:0, x2:0.85, y2:1,
        stop:0 rgba(17, 25, 37, 0.99), stop:1 rgba(10, 15, 23, 0.98));
    border-color: #2a3a4e;
}

QWidget#historyRow[tone="ok"] {
    border-color: rgba(63, 185, 80, 0.36);
}

QWidget#historyRow[tone="info"] {
    border-color: rgba(88, 166, 255, 0.32);
}

QWidget#historyRow[tone="accent"] {
    border-color: rgba(168, 85, 247, 0.34);
}

QWidget#historyRow[tone="warning"] {
    border-color: rgba(227, 179, 65, 0.34);
}

QWidget#historyRow[tone="error"] {
    border-color: rgba(248, 81, 73, 0.40);
}

QLabel#historyBadge {
    border-radius: 10px;
    padding: 4px 9px;
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 1px;
    color: #8b949e;
    background: rgba(71, 85, 105, 0.18);
    border: 1px solid rgba(100, 116, 139, 0.36);
}

QLabel#historyBadge[tone="neutral"] {
    color: #94a3b8;
    background: rgba(71, 85, 105, 0.18);
    border-color: rgba(100, 116, 139, 0.36);
}

QLabel#historyBadge[tone="ok"] {
    color: #4ade80;
    background: rgba(34, 197, 94, 0.13);
    border-color: rgba(63, 185, 80, 0.35);
}

QLabel#historyBadge[tone="info"] {
    color: #79c0ff;
    background: rgba(56, 139, 253, 0.14);
    border-color: rgba(88, 166, 255, 0.34);
}

QLabel#historyBadge[tone="accent"] {
    color: #c4b5fd;
    background: rgba(124, 58, 237, 0.14);
    border-color: rgba(168, 85, 247, 0.36);
}

QLabel#historyBadge[tone="warning"] {
    color: #fbbf24;
    background: rgba(245, 158, 11, 0.14);
    border-color: rgba(227, 179, 65, 0.38);
}

QLabel#historyBadge[tone="error"] {
    color: #ff7b72;
    background: rgba(248, 81, 73, 0.14);
    border-color: rgba(248, 81, 73, 0.40);
}

QLabel#historyTitle {
    color: #e6edf3;
    font-size: 14px;
    font-weight: 700;
    line-height: 1.4;
}

QLabel#historyPath {
    background: rgba(19, 28, 40, 0.78);
    border: 1px solid rgba(38, 65, 95, 0.72);
    border-radius: 10px;
    color: #9fd1ff;
    font-family: 'Cascadia Code', 'Consolas', monospace;
    font-size: 11px;
    padding: 7px 10px;
}

QLabel#historyMeta {
    color: #7d8590;
    font-size: 11px;
    line-height: 1.45;
}

    )");
}

QString scrollbarStyle() {
    return QStringLiteral(R"(

/* ── Scrollbars (vertical) ── */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 4px 2px 4px 2px;
}

QScrollBar::handle:vertical {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #1e2d3d, stop:1 #2a3a4e);
    border-radius: 5px;
    min-height: 36px;
}

QScrollBar::handle:vertical:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #2a3a4e, stop:1 #3d4f63);
}

QScrollBar::handle:vertical:pressed {
    background: #3d4f63;
}

/* ── Scrollbars (horizontal) ── */
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px 4px 2px 4px;
}

QScrollBar::handle:horizontal {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #1e2d3d, stop:1 #2a3a4e);
    border-radius: 5px;
    min-width: 36px;
}

QScrollBar::handle:horizontal:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 #2a3a4e, stop:1 #3d4f63);
}

QScrollBar::handle:horizontal:pressed {
    background: #3d4f63;
}

/* ── Scrollbar buttons (hide) ── */
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: transparent;
    border: none;
    width: 0;
    height: 0;
}

    )");
}

QString statusBarStyle() {
    return QStringLiteral(R"(

/* ── Status bar ── */
QStatusBar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #080c14, stop:0.5 #0d1117, stop:1 #080c14);
    border-top: 1px solid #1e2d3d;
    color: #5a6a7e;
    font-size: 11px;
    padding: 2px 8px;
}

QStatusBar::item {
    border: none;
}

    )");
}

QString pageStyle() {
    return QStringLiteral(R"(

/* ── Page headings ── */
QLabel#pageHeroTitle {
    color: #e6edf3;
    font-size: 32px;
    font-weight: 700;
    letter-spacing: -0.5px;
}

QLabel#pageTitle {
    color: #e6edf3;
    font-size: 28px;
    font-weight: 700;
    letter-spacing: -0.4px;
}

QLabel#pageSubtitle {
    color: #8b949e;
    font-size: 14px;
    line-height: 1.5;
}

QLabel#deepDiveEyebrow {
    color: #58a6ff;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1.8px;
    text-transform: uppercase;
}

QLabel#deepDiveSummary {
    color: #c7d1db;
    font-size: 13px;
    font-weight: 600;
    line-height: 1.45;
}

QLabel#sectionTitle {
    color: #e6edf3;
    font-size: 18px;
    font-weight: 700;
}

/* ── Text utility labels ── */
QLabel#boldText {
    color: #e6edf3;
    font-size: 14px;
    font-weight: 700;
}

QLabel#dimText {
    color: #8b949e;
    font-size: 12px;
}

QLabel#accentText {
    color: #3fb950;
    font-size: 12px;
    font-weight: 600;
}

    )");
}

QString fullStyleSheet() {
    return globalStyle()
         + chromeBarStyle()
         + sidebarStyle()
         + cardStyle()
         + inputStyle()
         + buttonStyle()
         + progressBarStyle()
         + chartStyle()
         + alertStyle()
         + scrollbarStyle()
         + statusBarStyle()
         + pageStyle();
}

}  // namespace StyleEngine
