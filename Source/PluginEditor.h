#pragma once

#include "PluginProcessor.h"
#include "UiKit.h"
#include "DeckStyle.h"
#include <bitset>
#include <functional>
#include <memory>
#include <vector>

// マウスで回せるレコード
class PlatterView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PlatterView (ScratchProcessor& p) : proc (p) {}

    bool japanese = false;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Point<float> centre() const;
    float radius() const;
    float angleAt (juce::Point<float>) const;

    ScratchProcessor& proc;
    bool dragging = false;
    float lastAng = 0.0f;
};

// 選択用のタイル (音源・パターン)。中に小さな絵を描く
class Tile : public juce::Component, public juce::SettableTooltipClient
{
public:
    using IconFn = std::function<void (juce::Graphics&, juce::Rectangle<float>, int index, bool selected)>;

    Tile (int index, std::function<void (int)> pick, std::function<int()> selected, IconFn icon)
        : idx (index), onPick (std::move (pick)), getSelected (std::move (selected)), drawIcon (std::move (icon)) {}

    void setLabel (const juce::String& t) { label = t; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onPick) onPick (idx); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    int idx;
    std::function<void (int)> onPick;
    std::function<int()> getSelected;
    IconFn drawIcon;
    juce::String label;
    bool hover = false;
};

// 音源の表示 (サンプルの波形と頭出し位置。クリックで頭出し位置を選べる)
// ホイールで拡大、右クリックで全体に戻す。拡大中は下の帯で見る位置を動かせる
class SourceView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SourceView (ScratchProcessor& p, std::function<void (float)> setStart) : proc (p), onSetStart (std::move (setStart)) {}

    bool japanese = false;

    void setView (double start, double length); // 見える範囲 (サンプル全体を 0〜1 として)
    double getViewStart() const noexcept { return viewStart; }
    double getViewLength() const noexcept { return viewLength; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics& g) override { deck::drawLcdGloss (g, getLocalBounds().toFloat()); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (4.0f, 14.0f); }
    juce::Rectangle<float> stripArea() const;
    bool sampleShown() const;
    bool zoomed() const noexcept { return viewLength < 0.9999; }
    void setFrom (const juce::MouseEvent&);
    void panFrom (const juce::MouseEvent&);
    void refreshPaths (juce::Rectangle<float> plot, juce::Rectangle<float> strip, float pixelScale);

    ScratchProcessor& proc;
    std::function<void (float)> onSetStart;
    double viewStart = 0.0, viewLength = 1.0;
    bool draggingStrip = false;

    // 波形の形は、見える範囲が変わった時だけ作り直す
    std::shared_ptr<const ScratchProcessor::WaveOverview> wave;
    juce::Path peakPath, rmsPath, stripPath;
    juce::Rectangle<float> pathPlot, pathStrip;
    double pathStart = -1.0, pathLength = -1.0;
    float pathScale = 0.0f;
};

// フィルのタイムライン (イベントのブロックを並べて表示。クリックで、そのブロックのパターンを切り替える)
// 空いているマスは、クリック/ドラッグで範囲を選ぶと、好きなパターンのブロックを追加できる
// 位置の単位はティック (1拍 = 24)。マス目の細かさ (gridTicks) は 1/16・1/32・3連8分・3連16分から選ぶ
class FillTimeline : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit FillTimeline (ScratchProcessor& p) : proc (p) {}

    bool japanese = false;
    int gridTicks = ScratchProcessor::ticksPerStep;  // マス目 (ティック)
    bool muteTool = false;                            // ミュート(消しゴム)ツール: なぞった所のフェーダーを閉じる
    std::function<void (int)> onEditPattern;          // 「この波形を編集」: パターンの番号

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics& g) override { deck::drawLcdGloss (g, getLocalBounds().toFloat()); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverEvent = -1; hoverCell = -1; setMouseCursor (juce::MouseCursor::NormalCursor); repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override; // ブロックの上: パターンを前後に

    void showSelection (int lo, int hi) { selLo = lo; selHi = hi; repaint(); } // 表示の確認用 (マスの番号)
    void showHover (int eventIndex) { hoverEvent = eventIndex; repaint(); }      // 表示の確認用 (マウスを乗せた表示)

private:
    enum class Drag { none, pending, depth, gate, start, move, select, mute, bendRise, bendFall, tail }; // pending = 押しただけ (動かした向きで、移動か振り幅かを決める)

    // ブロックの上でカーブを編集する (1周目に、始点・頂点の印、◇ = 曲がり具合、▼ = 鳴らす終わり)
    struct BlockGeom
    {
        juce::Rectangle<float> area; // カーブを描く所
        float cycleW = 0.0f;         // 1往復の幅 (px)
        float depthScale = 1.0f;
        float apex = 0.5f;
        bool ok = false;             // つまみを出せる大きさか
    };
    BlockGeom blockGeom (int eventIndex) const;
    juce::Point<float> curvePoint (int eventIndex, const BlockGeom&, float phi) const;
    float audibleEnd (int eventIndex, const BlockGeom&, float tail) const; // 1往復の中で、音が鳴り終わる位置
    int handleAt (int eventIndex, juce::Point<float>) const;               // 1 = 上りのカーブ、2 = 下りのカーブ、3 = 鳴らす終わり

    juce::Rectangle<float> laneArea() const;
    int totalTicks() const;
    float tickWidth() const;
    float tickAtX (float x) const;
    int eventAt (juce::Point<float>) const;
    int numCells() const;
    int cellAtX (float x) const;      // そのx位置のマス (範囲外は端に寄せる)
    bool cellFree (int cell) const;   // ブロックが無いマスか
    void showAddMenu (int lo, int hi);
    void showBlockMenu (int eventIndex);
    bool onGateEdge (int eventIndex, juce::Point<float>) const; // ブロックの右端(鳴らす長さの端)の近くか
    bool onStartEdge (int eventIndex, juce::Point<float>) const; // ブロックの左端の近くか
    void paintMute (float x, bool fine);

    ScratchProcessor& proc;
    int hoverEvent = -1;
    bool hoverEdge = false;
    bool hoverStartEdge = false;
    int hoverHandle = 0;
    float dragStartBend = 0.0f;

    Drag drag = Drag::none;
    int dragEvent = -1;
    bool dragMoved = false;
    bool editStarted = false;
    float dragStartY = 0.0f, dragStartDepth = 1.0f;
    float dragStartX = 0.0f, grabOffset = 0.0f; // 移動: ブロックの頭から、掴んだ所までの距離 (ティック)

    int hoverCell = -1;             // マウスの下の、空いているマス
    int selAnchor = -1;             // 範囲選択を始めたマス
    int selLo = -1, selHi = -1;     // 選んでいる範囲 (マス。両端を含む)
    int newRate = -1;               // 追加するブロックの速さ (-1 = 長さに合わせて自動)
    float wheelAccum = 0.0f;

    // ミュートのドラッグ: 始めた時のミュートと、なぞり始めの位置・塗る値
    std::bitset<ScratchProcessor::maxEventTicks> muteBase;
    float muteAnchorTick = 0.0f;
    bool mutePaint = true;
};

// 波形エディタ: すべてのパターン (決まった形 14 + ユーザー 4) の「動きのカーブ」と「フェーダー」を編集する
// 点をドラッグ / 空いた所をクリックで点を追加 / 区間の ◇ を上下にドラッグでカーブの曲がり具合 (右クリックで種類)
// フェーダー: 下の帯を塗る / 鳴る区間の端の ▼ を左右にドラッグで伸び縮み
class PatternEditorView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PatternEditorView (ScratchProcessor& p);

    bool japanese = false;
    int pattern = ScratchProcessor::numPresetPatterns; // 編集しているパターン

    void setPattern (int p);
    void refreshList();   // 一覧 (言語・編集済みの印) を作り直す

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverPoint = hoverSeg = hoverEdge = -1; repaint(); }

private:
    enum class Drag { none, point, tension, gatePaint, gateEdge };

    juce::Rectangle<float> thumbArea() const;
    juce::Rectangle<float> canvasArea() const;
    juce::Rectangle<float> gateArea() const;
    juce::Point<float> toScreen (juce::Point<float> p) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    int pointAt (juce::Point<float> screenPos) const;
    juce::Point<float> handlePos (int segment) const;
    int handleAt (juce::Point<float> screenPos) const;
    std::vector<int> gateEdges() const;
    float edgeX (int gateIndex) const;
    int gateEdgeAt (juce::Point<float> screenPos) const;
    int segmentAtX (float x) const;
    void showCurveMenu (int segment);

    ScratchProcessor& proc;
    juce::ComboBox patternBox;
    juce::TextButton resetButton;
    Drag drag = Drag::none;
    int dragPoint = -1, hoverPoint = -1;
    int dragSeg = -1, hoverSeg = -1;
    int dragEdge = -1, hoverEdge = -1;
    float dragStartY = 0.0f, dragStartTension = 0.0f;
    bool gatePaintValue = true;
    int lastSegment = -1;
    std::array<bool, ScratchProcessor::userGateSegments> gateAtDown {};
};

// テンプレートの一覧。開く前に、マイパターンを読み直す (ほかの Cue Burn で保存した分も出す)
class TemplateBox : public juce::ComboBox
{
public:
    std::function<void()> beforePopup;
    void showPopup() override
    {
        if (beforePopup != nullptr)
            beforePopup();
        juce::ComboBox::showPopup();
    }
};

// マイパターンの名前の入力・確認 (画面の上に重ねて出す)
class PatternPrompt : public juce::Component
{
public:
    PatternPrompt();
    void open (const juce::String& title, const juce::String& message, bool askName, const juce::String& name,
               const juce::String& okText, const juce::String& cancelText, std::function<void (juce::String)> onOk);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // 後ろのものを触らせない
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> boxBounds() const;
    void accept();

    juce::String title, message;
    juce::TextEditor field;
    juce::TextButton okButton, cancelButton;
    std::function<void (juce::String)> action;
};

class ScratchEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ScratchEditor (ScratchProcessor&);
    ~ScratchEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    FillTimeline& getTimeline() noexcept { return timeline; } // 表示の確認用
    SourceView& getSourceView() noexcept { return sourceView; } // 表示の確認用
    void showPatternEditor (int pattern);                     // 波形エディタを開く (パターンの番号)
    void applySkin();                                         // 今の見た目を画面に反映する
    void askSavePattern();                                    // マイパターン: 名前を聞いて保存

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void paintHeader (juce::Graphics&);
    void setLanguage (bool japanese);
    void applyLanguage();
    void setChoice (const char* id, int index);
    void setFloat (const char* id, float value);
    void loadFile (const juce::File&);
    void openLoadDialog();
    void refreshFillControls();
    void stepTemplate (int delta);
    void rebuildTemplateList();
    void showMyPatternMenu();
    void savePattern (const juce::String& name);
    void askDeletePattern (const juce::String& name);

    struct Knob
    {
        juce::String id;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        HelpLabel label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    struct Combo
    {
        juce::String id;
        juce::ComboBox box;
        HelpLabel label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
    };

    struct Panel
    {
        juce::Rectangle<int> area;
        int titleIndex;
        juce::Colour colour;
        std::vector<Knob*> knobs;
    };

    Knob& addKnob (const juce::String& id, juce::Colour colour);
    Combo& addCombo (const juce::String& id, juce::Colour colour);
    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    DeckLookAndFeel laf; // コンポーネントより先に宣言 (最後に破棄される)。CDJ 風の見た目
    ScratchProcessor& proc;
    juce::TooltipWindow tooltipWindow { this, 350 };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<Panel> panels;
    Combo* rateCombo = nullptr;
    Knob* startKnob = nullptr;

    PlatterView platter;
    SourceView sourceView;
    FillTimeline timeline;
    PatternEditorView patternEditor;
    std::array<std::unique_ptr<Tile>, ScratchProcessor::numSources> sourceTiles;
    std::array<std::unique_ptr<Tile>, ScratchProcessor::numPatterns> patternTiles;
    juce::TextButton loadButton;
    juce::ComboBox presetBox;      // プリセットサンプル
    int shownPreset = -2;
    std::unique_ptr<juce::FileChooser> chooser;

    // フィルの操作
    juce::TextButton modeFill, modeSingle, modeEdit;
    bool editorView = false;          // 波形エディタを表示中 (モードの設定は変えない)
    juce::ComboBox gridBox, copyBox;  // マス目の細かさ / 波形エディタ: コピー元
    juce::Label gridLabel;
    juce::TextButton muteButton;      // ミュート(消しゴム)ツール
    juce::TextButton tmplPrev, tmplNext;
    TemplateBox tmplBox;
    juce::TextButton myButton;        // マイパターン
    juce::StringArray myNames;        // 一覧に出しているマイパターン
    PatternPrompt prompt;
    std::array<juce::TextButton, 3> barButtons;
    juce::TextButton randomButton, varyButton, backButton, previewButton;
    int shownPhraseVersion = -1;

    juce::TextButton enButton, jaButton, skinButton;
    deck::Skin shownSkin = deck::Skin::deck;
    bool japanese = false;
    bool dragHover = false;

    juce::Rectangle<int> header, platterArea, sourceArea, fillArea, fillLowerArea;

    float shownOut = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchEditor)
};
