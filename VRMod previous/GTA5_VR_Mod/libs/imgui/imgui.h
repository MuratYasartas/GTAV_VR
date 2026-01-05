// Dear ImGui placeholder header
// This is a minimal placeholder for compilation
// Download actual library from https://github.com/ocornut/imgui

#ifndef IMGUI_H
#define IMGUI_H

#include <stddef.h>
#include <stdint.h>

#define IMGUI_VERSION               "1.89.9"
#define IMGUI_VERSION_NUM           18909
#define IMGUI_CHECKVERSION()        ImGui::DebugCheckVersionAndDataLayout(IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(unsigned int))

// Forward declarations
struct ImDrawChannel;
struct ImDrawCmd;
struct ImDrawData;
struct ImDrawList;
struct ImDrawListSharedData;
struct ImDrawListSplitter;
struct ImDrawVert;
struct ImFont;
struct ImFontAtlas;
struct ImFontConfig;
struct ImFontGlyph;
struct ImFontGlyphRangesBuilder;
struct ImGuiContext;
struct ImGuiIO;
struct ImGuiInputTextCallbackData;
struct ImGuiKeyData;
struct ImGuiListClipper;
struct ImGuiOnceUponAFrame;
struct ImGuiPayload;
struct ImGuiPlatformIO;
struct ImGuiSizeCallbackData;
struct ImGuiStorage;
struct ImGuiStyle;
struct ImGuiTableSortSpecs;
struct ImGuiTableColumnSortSpecs;
struct ImGuiTextBuffer;
struct ImGuiTextFilter;
struct ImGuiViewport;
struct ImGuiWindowClass;

// Type definitions
typedef int ImGuiCol;
typedef int ImGuiCond;
typedef int ImGuiDataType;
typedef int ImGuiDir;
typedef int ImGuiKey;
typedef int ImGuiMouseButton;
typedef int ImGuiMouseCursor;
typedef int ImGuiSortDirection;
typedef int ImGuiStyleVar;
typedef int ImGuiTableBgTarget;
typedef int ImGuiTableColumnFlags;
typedef int ImGuiTableFlags;
typedef int ImGuiTableRowFlags;
typedef int ImGuiTreeNodeFlags;
typedef int ImGuiViewportFlags;
typedef int ImGuiWindowFlags;

typedef void* ImTextureID;
typedef unsigned int ImGuiID;
typedef int (*ImGuiInputTextCallback)(ImGuiInputTextCallbackData* data);
typedef void (*ImGuiSizeCallback)(ImGuiSizeCallbackData* data);

typedef unsigned short ImDrawIdx;
typedef unsigned int ImWchar32;
typedef unsigned short ImWchar16;
typedef ImWchar32 ImWchar;
typedef signed char ImS8;
typedef unsigned char ImU8;
typedef signed short ImS16;
typedef unsigned short ImU16;
typedef signed int ImS32;
typedef unsigned int ImU32;
typedef signed long long ImS64;
typedef unsigned long long ImU64;

// ImVec2/ImVec4
struct ImVec2 {
    float x, y;
    ImVec2() : x(0.0f), y(0.0f) {}
    ImVec2(float _x, float _y) : x(_x), y(_y) {}
};

struct ImVec4 {
    float x, y, z, w;
    ImVec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

// Window flags
enum ImGuiWindowFlags_ {
    ImGuiWindowFlags_None                   = 0,
    ImGuiWindowFlags_NoTitleBar             = 1 << 0,
    ImGuiWindowFlags_NoResize               = 1 << 1,
    ImGuiWindowFlags_NoMove                 = 1 << 2,
    ImGuiWindowFlags_NoScrollbar            = 1 << 3,
    ImGuiWindowFlags_NoScrollWithMouse      = 1 << 4,
    ImGuiWindowFlags_NoCollapse             = 1 << 5,
    ImGuiWindowFlags_AlwaysAutoResize       = 1 << 6,
    ImGuiWindowFlags_NoBackground           = 1 << 7,
    ImGuiWindowFlags_NoSavedSettings        = 1 << 8,
    ImGuiWindowFlags_NoMouseInputs          = 1 << 9,
    ImGuiWindowFlags_MenuBar                = 1 << 10,
    ImGuiWindowFlags_HorizontalScrollbar    = 1 << 11,
    ImGuiWindowFlags_NoFocusOnAppearing     = 1 << 12,
    ImGuiWindowFlags_NoBringToFrontOnFocus  = 1 << 13,
    ImGuiWindowFlags_AlwaysVerticalScrollbar= 1 << 14,
    ImGuiWindowFlags_AlwaysHorizontalScrollbar = 1 << 15,
    ImGuiWindowFlags_AlwaysUseWindowPadding = 1 << 16,
    ImGuiWindowFlags_NoNavInputs            = 1 << 18,
    ImGuiWindowFlags_NoNavFocus             = 1 << 19,
    ImGuiWindowFlags_UnsavedDocument        = 1 << 20,
    ImGuiWindowFlags_NoNav                  = ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus,
    ImGuiWindowFlags_NoDecoration           = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse,
    ImGuiWindowFlags_NoInputs               = ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus,
};

// Color IDs
enum ImGuiCol_ {
    ImGuiCol_Text,
    ImGuiCol_TextDisabled,
    ImGuiCol_WindowBg,
    ImGuiCol_ChildBg,
    ImGuiCol_PopupBg,
    ImGuiCol_Border,
    ImGuiCol_BorderShadow,
    ImGuiCol_FrameBg,
    ImGuiCol_FrameBgHovered,
    ImGuiCol_FrameBgActive,
    ImGuiCol_TitleBg,
    ImGuiCol_TitleBgActive,
    ImGuiCol_TitleBgCollapsed,
    ImGuiCol_MenuBarBg,
    ImGuiCol_ScrollbarBg,
    ImGuiCol_ScrollbarGrab,
    ImGuiCol_ScrollbarGrabHovered,
    ImGuiCol_ScrollbarGrabActive,
    ImGuiCol_CheckMark,
    ImGuiCol_SliderGrab,
    ImGuiCol_SliderGrabActive,
    ImGuiCol_Button,
    ImGuiCol_ButtonHovered,
    ImGuiCol_ButtonActive,
    ImGuiCol_Header,
    ImGuiCol_HeaderHovered,
    ImGuiCol_HeaderActive,
    ImGuiCol_Separator,
    ImGuiCol_SeparatorHovered,
    ImGuiCol_SeparatorActive,
    ImGuiCol_ResizeGrip,
    ImGuiCol_ResizeGripHovered,
    ImGuiCol_ResizeGripActive,
    ImGuiCol_Tab,
    ImGuiCol_TabHovered,
    ImGuiCol_TabActive,
    ImGuiCol_TabUnfocused,
    ImGuiCol_TabUnfocusedActive,
    ImGuiCol_PlotLines,
    ImGuiCol_PlotLinesHovered,
    ImGuiCol_PlotHistogram,
    ImGuiCol_PlotHistogramHovered,
    ImGuiCol_TableHeaderBg,
    ImGuiCol_TableBorderStrong,
    ImGuiCol_TableBorderLight,
    ImGuiCol_TableRowBg,
    ImGuiCol_TableRowBgAlt,
    ImGuiCol_TextSelectedBg,
    ImGuiCol_DragDropTarget,
    ImGuiCol_NavHighlight,
    ImGuiCol_NavWindowingHighlight,
    ImGuiCol_NavWindowingDimBg,
    ImGuiCol_ModalWindowDimBg,
    ImGuiCol_COUNT
};

// Style structure
struct ImGuiStyle {
    float       Alpha;
    float       DisabledAlpha;
    ImVec2      WindowPadding;
    float       WindowRounding;
    float       WindowBorderSize;
    ImVec2      WindowMinSize;
    ImVec2      WindowTitleAlign;
    int         WindowMenuButtonPosition;
    float       ChildRounding;
    float       ChildBorderSize;
    float       PopupRounding;
    float       PopupBorderSize;
    ImVec2      FramePadding;
    float       FrameRounding;
    float       FrameBorderSize;
    ImVec2      ItemSpacing;
    ImVec2      ItemInnerSpacing;
    ImVec2      CellPadding;
    ImVec2      TouchExtraPadding;
    float       IndentSpacing;
    float       ColumnsMinSpacing;
    float       ScrollbarSize;
    float       ScrollbarRounding;
    float       GrabMinSize;
    float       GrabRounding;
    float       LogSliderDeadzone;
    float       TabRounding;
    float       TabBorderSize;
    float       TabMinWidthForCloseButton;
    int         ColorButtonPosition;
    ImVec2      ButtonTextAlign;
    ImVec2      SelectableTextAlign;
    ImVec2      DisplayWindowPadding;
    ImVec2      DisplaySafeAreaPadding;
    float       MouseCursorScale;
    bool        AntiAliasedLines;
    bool        AntiAliasedLinesUseTex;
    bool        AntiAliasedFill;
    float       CurveTessellationTol;
    float       CircleTessellationMaxError;
    ImVec4      Colors[ImGuiCol_COUNT];
};

// IO structure
struct ImGuiIO {
    // Configuration
    int         ConfigFlags;
    int         BackendFlags;
    ImVec2      DisplaySize;
    float       DeltaTime;
    float       IniSavingRate;
    const char* IniFilename;
    const char* LogFilename;
    float       MouseDoubleClickTime;
    float       MouseDoubleClickMaxDist;
    float       MouseDragThreshold;
    float       KeyRepeatDelay;
    float       KeyRepeatRate;
    void*       UserData;

    // Input state
    ImVec2      MousePos;
    bool        MouseDown[5];
    float       MouseWheel;
    float       MouseWheelH;
    bool        KeyCtrl;
    bool        KeyShift;
    bool        KeyAlt;
    bool        KeySuper;
    bool        KeysDown[512];

    // Output
    bool        WantCaptureMouse;
    bool        WantCaptureKeyboard;
    bool        WantTextInput;
    bool        WantSetMousePos;
    bool        WantSaveIniSettings;
    bool        NavActive;
    bool        NavVisible;
    float       Framerate;
    int         MetricsRenderVertices;
    int         MetricsRenderIndices;
    int         MetricsRenderWindows;
    int         MetricsActiveWindows;
    int         MetricsActiveAllocations;
    ImVec2      MouseDelta;
};

// Draw vertex
struct ImDrawVert {
    ImVec2  pos;
    ImVec2  uv;
    ImU32   col;
};

// Main ImGui namespace
namespace ImGui {
    // Context creation/destruction
    ImGuiContext* CreateContext(ImFontAtlas* shared_font_atlas = NULL);
    void DestroyContext(ImGuiContext* ctx = NULL);
    ImGuiContext* GetCurrentContext();
    void SetCurrentContext(ImGuiContext* ctx);

    // Main
    ImGuiIO& GetIO();
    ImGuiStyle& GetStyle();
    void NewFrame();
    void EndFrame();
    void Render();
    ImDrawData* GetDrawData();

    // Debug utilities
    void ShowDemoWindow(bool* p_open = NULL);
    void ShowMetricsWindow(bool* p_open = NULL);
    void ShowStackToolWindow(bool* p_open = NULL);
    void ShowAboutWindow(bool* p_open = NULL);
    void ShowStyleEditor(ImGuiStyle* ref = NULL);
    bool ShowStyleSelector(const char* label);
    void ShowFontSelector(const char* label);
    void ShowUserGuide();
    const char* GetVersion();

    // Windows
    bool Begin(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0);
    void End();

    // Child Windows
    bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0, 0), bool border = false, ImGuiWindowFlags flags = 0);
    bool BeginChild(ImGuiID id, const ImVec2& size = ImVec2(0, 0), bool border = false, ImGuiWindowFlags flags = 0);
    void EndChild();

    // Windows Utilities
    bool IsWindowAppearing();
    bool IsWindowCollapsed();
    bool IsWindowFocused(int flags = 0);
    bool IsWindowHovered(int flags = 0);
    ImDrawList* GetWindowDrawList();
    ImVec2 GetWindowPos();
    ImVec2 GetWindowSize();
    float GetWindowWidth();
    float GetWindowHeight();

    // Window manipulation
    void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond = 0, const ImVec2& pivot = ImVec2(0, 0));
    void SetNextWindowSize(const ImVec2& size, ImGuiCond cond = 0);
    void SetNextWindowSizeConstraints(const ImVec2& size_min, const ImVec2& size_max, ImGuiSizeCallback custom_callback = NULL, void* custom_callback_data = NULL);
    void SetNextWindowContentSize(const ImVec2& size);
    void SetNextWindowCollapsed(bool collapsed, ImGuiCond cond = 0);
    void SetNextWindowFocus();
    void SetNextWindowBgAlpha(float alpha);
    void SetWindowPos(const ImVec2& pos, ImGuiCond cond = 0);
    void SetWindowSize(const ImVec2& size, ImGuiCond cond = 0);
    void SetWindowCollapsed(bool collapsed, ImGuiCond cond = 0);
    void SetWindowFocus();
    void SetWindowFontScale(float scale);
    void SetWindowPos(const char* name, const ImVec2& pos, ImGuiCond cond = 0);
    void SetWindowSize(const char* name, const ImVec2& size, ImGuiCond cond = 0);
    void SetWindowCollapsed(const char* name, bool collapsed, ImGuiCond cond = 0);
    void SetWindowFocus(const char* name);

    // Content region
    ImVec2 GetContentRegionAvail();
    ImVec2 GetContentRegionMax();
    ImVec2 GetWindowContentRegionMin();
    ImVec2 GetWindowContentRegionMax();

    // Cursor/Layout
    void Separator();
    void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
    void NewLine();
    void Spacing();
    void Dummy(const ImVec2& size);
    void Indent(float indent_w = 0.0f);
    void Unindent(float indent_w = 0.0f);
    void BeginGroup();
    void EndGroup();
    ImVec2 GetCursorPos();
    float GetCursorPosX();
    float GetCursorPosY();
    void SetCursorPos(const ImVec2& local_pos);
    void SetCursorPosX(float local_x);
    void SetCursorPosY(float local_y);
    ImVec2 GetCursorStartPos();
    ImVec2 GetCursorScreenPos();
    void SetCursorScreenPos(const ImVec2& pos);
    void AlignTextToFramePadding();
    float GetTextLineHeight();
    float GetTextLineHeightWithSpacing();
    float GetFrameHeight();
    float GetFrameHeightWithSpacing();

    // ID stack/scopes
    void PushID(const char* str_id);
    void PushID(const char* str_id_begin, const char* str_id_end);
    void PushID(const void* ptr_id);
    void PushID(int int_id);
    void PopID();
    ImGuiID GetID(const char* str_id);
    ImGuiID GetID(const char* str_id_begin, const char* str_id_end);
    ImGuiID GetID(const void* ptr_id);

    // Widgets: Text
    void TextUnformatted(const char* text, const char* text_end = NULL);
    void Text(const char* fmt, ...);
    void TextV(const char* fmt, va_list args);
    void TextColored(const ImVec4& col, const char* fmt, ...);
    void TextColoredV(const ImVec4& col, const char* fmt, va_list args);
    void TextDisabled(const char* fmt, ...);
    void TextDisabledV(const char* fmt, va_list args);
    void TextWrapped(const char* fmt, ...);
    void TextWrappedV(const char* fmt, va_list args);
    void LabelText(const char* label, const char* fmt, ...);
    void LabelTextV(const char* label, const char* fmt, va_list args);
    void BulletText(const char* fmt, ...);
    void BulletTextV(const char* fmt, va_list args);

    // Widgets: Main
    bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool SmallButton(const char* label);
    bool InvisibleButton(const char* str_id, const ImVec2& size, int flags = 0);
    bool ArrowButton(const char* str_id, ImGuiDir dir);
    bool Checkbox(const char* label, bool* v);
    bool CheckboxFlags(const char* label, int* flags, int flags_value);
    bool CheckboxFlags(const char* label, unsigned int* flags, unsigned int flags_value);
    bool RadioButton(const char* label, bool active);
    bool RadioButton(const char* label, int* v, int v_button);
    void ProgressBar(float fraction, const ImVec2& size_arg = ImVec2(-1.0f, 0.0f), const char* overlay = NULL);
    void Bullet();

    // Widgets: Combo Box
    bool BeginCombo(const char* label, const char* preview_value, int flags = 0);
    void EndCombo();
    bool Combo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1);
    bool Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1);
    bool Combo(const char* label, int* current_item, bool(*items_getter)(void* data, int idx, const char** out_text), void* data, int items_count, int popup_max_height_in_items = -1);

    // Widgets: Drag Sliders
    bool DragFloat(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", int flags = 0);
    bool DragFloat2(const char* label, float v[2], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", int flags = 0);
    bool DragFloat3(const char* label, float v[3], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", int flags = 0);
    bool DragFloat4(const char* label, float v[4], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", int flags = 0);
    bool DragInt(const char* label, int* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", int flags = 0);
    bool DragInt2(const char* label, int v[2], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", int flags = 0);
    bool DragInt3(const char* label, int v[3], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", int flags = 0);
    bool DragInt4(const char* label, int v[4], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", int flags = 0);

    // Widgets: Regular Sliders
    bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", int flags = 0);
    bool SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format = "%.3f", int flags = 0);
    bool SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format = "%.3f", int flags = 0);
    bool SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format = "%.3f", int flags = 0);
    bool SliderAngle(const char* label, float* v_rad, float v_degrees_min = -360.0f, float v_degrees_max = +360.0f, const char* format = "%.0f deg", int flags = 0);
    bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", int flags = 0);
    bool SliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format = "%d", int flags = 0);
    bool SliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format = "%d", int flags = 0);
    bool SliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format = "%d", int flags = 0);

    // Widgets: Input with Keyboard
    bool InputText(const char* label, char* buf, size_t buf_size, int flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
    bool InputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2& size = ImVec2(0, 0), int flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
    bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, int flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL);
    bool InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", int flags = 0);
    bool InputFloat2(const char* label, float v[2], const char* format = "%.3f", int flags = 0);
    bool InputFloat3(const char* label, float v[3], const char* format = "%.3f", int flags = 0);
    bool InputFloat4(const char* label, float v[4], const char* format = "%.3f", int flags = 0);
    bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100, int flags = 0);
    bool InputInt2(const char* label, int v[2], int flags = 0);
    bool InputInt3(const char* label, int v[3], int flags = 0);
    bool InputInt4(const char* label, int v[4], int flags = 0);
    bool InputDouble(const char* label, double* v, double step = 0.0, double step_fast = 0.0, const char* format = "%.6f", int flags = 0);

    // Widgets: Color Editor/Picker
    bool ColorEdit3(const char* label, float col[3], int flags = 0);
    bool ColorEdit4(const char* label, float col[4], int flags = 0);
    bool ColorPicker3(const char* label, float col[3], int flags = 0);
    bool ColorPicker4(const char* label, float col[4], int flags = 0, const float* ref_col = NULL);
    bool ColorButton(const char* desc_id, const ImVec4& col, int flags = 0, const ImVec2& size = ImVec2(0, 0));
    void SetColorEditOptions(int flags);

    // Widgets: Trees
    bool TreeNode(const char* label);
    bool TreeNode(const char* str_id, const char* fmt, ...);
    bool TreeNode(const void* ptr_id, const char* fmt, ...);
    bool TreeNodeV(const char* str_id, const char* fmt, va_list args);
    bool TreeNodeV(const void* ptr_id, const char* fmt, va_list args);
    bool TreeNodeEx(const char* label, int flags = 0);
    bool TreeNodeEx(const char* str_id, int flags, const char* fmt, ...);
    bool TreeNodeEx(const void* ptr_id, int flags, const char* fmt, ...);
    bool TreeNodeExV(const char* str_id, int flags, const char* fmt, va_list args);
    bool TreeNodeExV(const void* ptr_id, int flags, const char* fmt, va_list args);
    void TreePush(const char* str_id);
    void TreePush(const void* ptr_id = NULL);
    void TreePop();
    float GetTreeNodeToLabelSpacing();
    bool CollapsingHeader(const char* label, int flags = 0);
    bool CollapsingHeader(const char* label, bool* p_visible, int flags = 0);
    void SetNextItemOpen(bool is_open, ImGuiCond cond = 0);

    // Widgets: Selectables
    bool Selectable(const char* label, bool selected = false, int flags = 0, const ImVec2& size = ImVec2(0, 0));
    bool Selectable(const char* label, bool* p_selected, int flags = 0, const ImVec2& size = ImVec2(0, 0));

    // Widgets: List Boxes
    bool BeginListBox(const char* label, const ImVec2& size = ImVec2(0, 0));
    void EndListBox();
    bool ListBox(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items = -1);
    bool ListBox(const char* label, int* current_item, bool (*items_getter)(void* data, int idx, const char** out_text), void* data, int items_count, int height_in_items = -1);

    // Widgets: Menus
    bool BeginMenuBar();
    void EndMenuBar();
    bool BeginMainMenuBar();
    void EndMainMenuBar();
    bool BeginMenu(const char* label, bool enabled = true);
    void EndMenu();
    bool MenuItem(const char* label, const char* shortcut = NULL, bool selected = false, bool enabled = true);
    bool MenuItem(const char* label, const char* shortcut, bool* p_selected, bool enabled = true);

    // Tooltips
    void BeginTooltip();
    void EndTooltip();
    void SetTooltip(const char* fmt, ...);
    void SetTooltipV(const char* fmt, va_list args);

    // Popups, Modals
    bool BeginPopup(const char* str_id, int flags = 0);
    bool BeginPopupModal(const char* name, bool* p_open = NULL, int flags = 0);
    void EndPopup();
    void OpenPopup(const char* str_id, int popup_flags = 0);
    void OpenPopup(ImGuiID id, int popup_flags = 0);
    void OpenPopupOnItemClick(const char* str_id = NULL, int popup_flags = 1);
    void CloseCurrentPopup();
    bool BeginPopupContextItem(const char* str_id = NULL, int popup_flags = 1);
    bool BeginPopupContextWindow(const char* str_id = NULL, int popup_flags = 1);
    bool BeginPopupContextVoid(const char* str_id = NULL, int popup_flags = 1);
    bool IsPopupOpen(const char* str_id, int flags = 0);

    // Tables
    bool BeginTable(const char* str_id, int column, int flags = 0, const ImVec2& outer_size = ImVec2(0.0f, 0.0f), float inner_width = 0.0f);
    void EndTable();
    void TableNextRow(int row_flags = 0, float min_row_height = 0.0f);
    bool TableNextColumn();
    bool TableSetColumnIndex(int column_n);
    void TableSetupColumn(const char* label, int flags = 0, float init_width_or_weight = 0.0f, ImGuiID user_id = 0);
    void TableSetupScrollFreeze(int cols, int rows);
    void TableHeadersRow();
    void TableHeader(const char* label);

    // Focus, Activation
    void SetItemDefaultFocus();
    void SetKeyboardFocusHere(int offset = 0);

    // Item/Widgets Utilities
    bool IsItemHovered(int flags = 0);
    bool IsItemActive();
    bool IsItemFocused();
    bool IsItemClicked(int mouse_button = 0);
    bool IsItemVisible();
    bool IsItemEdited();
    bool IsItemActivated();
    bool IsItemDeactivated();
    bool IsItemDeactivatedAfterEdit();
    bool IsItemToggledOpen();
    bool IsAnyItemHovered();
    bool IsAnyItemActive();
    bool IsAnyItemFocused();
    ImVec2 GetItemRectMin();
    ImVec2 GetItemRectMax();
    ImVec2 GetItemRectSize();
    void SetItemAllowOverlap();

    // Miscellaneous Utilities
    bool IsRectVisible(const ImVec2& size);
    bool IsRectVisible(const ImVec2& rect_min, const ImVec2& rect_max);
    double GetTime();
    int GetFrameCount();
    ImDrawList* GetBackgroundDrawList();
    ImDrawList* GetForegroundDrawList();
    ImDrawListSharedData* GetDrawListSharedData();
    const char* GetStyleColorName(ImGuiCol idx);
    void SetStateStorage(ImGuiStorage* storage);
    ImGuiStorage* GetStateStorage();
    bool BeginChildFrame(ImGuiID id, const ImVec2& size, int flags = 0);
    void EndChildFrame();

    // Color Utilities
    ImVec4 ColorConvertU32ToFloat4(ImU32 in);
    ImU32 ColorConvertFloat4ToU32(const ImVec4& in);
    void ColorConvertRGBtoHSV(float r, float g, float b, float& out_h, float& out_s, float& out_v);
    void ColorConvertHSVtoRGB(float h, float s, float v, float& out_r, float& out_g, float& out_b);

    // Debug
    void DebugCheckVersionAndDataLayout(const char* version_str, size_t sz_io, size_t sz_style, size_t sz_vec2, size_t sz_vec4, size_t sz_drawvert, size_t sz_drawidx);
}

#endif // IMGUI_H
