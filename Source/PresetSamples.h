#pragma once

// プリセットサンプルの一覧 (tools/scratch_samples/build_presets.py が書き出す。手で直さない)
// 声: Sound Artz Voice Samples (Suno 製、商用OK)。それ以外: Stable Audio 3 で生成 (Stability AI Community License)

struct PresetSampleInfo
{
    const char* resource; // BinaryData の名前
    int group;
    const char* en;
    const char* ja;
};

inline const char* const kPresetGroups[][2] = {
    { "VOICE", "ボイス" },
    { "HORN", "ホーン" },
    { "ORCHESTRA HIT", "オーケストラヒット" },
    { "SYNTH", "シンセ" },
    { "808 BASS", "808 ベース" },
    { "GUITAR", "ギター" },
};

inline const PresetSampleInfo kPresetSamples[] = {
    { "v01_ha_flac", 0, "Ha", "Ha" },
    { "v02_haw_flac", 0, "Haw", "Haw" },
    { "v03_oh_flac", 0, "Oh", "Oh" },
    { "v04_go_flac", 0, "Go", "Go" },
    { "v05_step_flac", 0, "Step", "Step" },
    { "v06_listen_flac", 0, "Listen", "Listen" },
    { "v07_yeah_flac", 0, "Yeah", "Yeah" },
    { "v08_checkitout_flac", 0, "Check It Out", "Check It Out" },
    { "v09_herewego_flac", 0, "Here We Go", "Here We Go" },
    { "v10_letsgo_flac", 0, "Let's Go", "Let's Go" },
    { "v11_dropthebeat_flac", 0, "Drop The Beat", "Drop The Beat" },
    { "v12_turnitup_flac", 0, "Turn It Up", "Turn It Up" },
    { "a_horn_stab_1_flac", 1, "Horn Stab 1", "ホーン 1" },
    { "a_horn_stab_2_flac", 1, "Horn Stab 2", "ホーン 2" },
    { "a_horn_stab_3_flac", 1, "Horn Stab 3", "ホーン 3" },
    { "a_orch_hit_1_flac", 2, "Orchestra Hit 1", "オーケストラヒット 1" },
    { "a_orch_hit_2_flac", 2, "Orchestra Hit 2", "オーケストラヒット 2" },
    { "a_orch_hit_3_flac", 2, "Orchestra Hit 3", "オーケストラヒット 3" },
    { "a_synth_stab_1_flac", 3, "Synth Stab 1", "シンセ・スタブ 1" },
    { "a_synth_stab_2_flac", 3, "Synth Stab 2", "シンセ・スタブ 2" },
    { "a_synth_stab_3_flac", 3, "Synth Stab 3", "シンセ・スタブ 3" },
    { "a_808_bass_1_flac", 4, "808 Bass 1", "808 ベース 1" },
    { "a_808_bass_2_flac", 4, "808 Bass 2", "808 ベース 2" },
    { "a_808_bass_3_flac", 4, "808 Bass 3", "808 ベース 3" },
    { "a_guitar_stab_1_flac", 5, "Guitar Stab 1", "ギター 1" },
    { "a_guitar_stab_2_flac", 5, "Guitar Stab 2", "ギター 2" },
    { "a_guitar_stab_3_flac", 5, "Guitar Stab 3", "ギター 3" },
};

constexpr int kNumPresetSamples = (int) (sizeof (kPresetSamples) / sizeof (kPresetSamples[0]));
