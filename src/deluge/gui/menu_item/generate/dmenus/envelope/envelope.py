from dmui.dsl import Menu

byEnvelope = {}

for i in range(4):
    byEnvelope[i] = [
        Menu(
            "envelope::Segment",
            f"env{i + 1}AttackMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_ATTACK", f"{i}"],
            "envelope/attack.md",
            name="STRING_FOR_ATTACK",
            title="STRING_FOR_ENV_ATTACK_MENU_TITLE",
        ),
        Menu(
            "envelope::Segment",
            f"env{i + 1}DecayMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_DECAY", f"{i}"],
            "envelope/decay.md",
            name="STRING_FOR_DECAY",
            title="STRING_FOR_ENV_DECAY_MENU_TITLE",
        ),
        Menu(
            "envelope::Segment",
            f"env{i + 1}SustainMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_SUSTAIN", f"{i}"],
            "envelope/sustain.md",
            name="STRING_FOR_SUSTAIN",
            title="STRING_FOR_ENV_SUSTAIN_MENU_TITLE",
        ),
        Menu(
            "envelope::Segment",
            f"env{i + 1}ReleaseMenu",
            ["{name}", "{title}", "params::LOCAL_ENV_0_RELEASE", f"{i}"],
            "envelope/release.md",
            name="STRING_FOR_RELEASE",
            title="STRING_FOR_ENV_RELEASE_MENU_TITLE",
        ),
    ]

# Kick synth mode: envelopes 3 and 4 are pitch envelopes, with these controls instead of attack / decay / sustain /
# release (see gui/menu_item/envelope/kick_pitch.h)
for i in range(2, 4):
    byEnvelope[i] += [
        Menu(
            "envelope::kick_pitch::Amount",
            f"env{i + 1}KickPitchAmountMenu",
            ["{name}", "{title}", f"{i}"],
            "envelope/kick_pitch.md",
            name="STRING_FOR_PITCH",
            title="STRING_FOR_ENV_PITCH_MENU_TITLE",
            available_when="The synth mode is Kick",
        ),
        Menu(
            "envelope::kick_pitch::Time",
            f"env{i + 1}KickPitchTimeMenu",
            ["{name}", "{title}", f"{i}"],
            "envelope/kick_pitch.md",
            name="STRING_FOR_TIME",
            title="STRING_FOR_ENV_TIME_MENU_TITLE",
            available_when="The synth mode is Kick",
        ),
        Menu(
            "envelope::kick_pitch::Curve",
            f"env{i + 1}KickPitchCurveMenu",
            ["{name}", "{title}", f"{i}"],
            "envelope/kick_pitch.md",
            name="STRING_FOR_CURVE",
            title="STRING_FOR_ENV_CURVE_MENU_TITLE",
            available_when="The synth mode is Kick",
        ),
    ]
