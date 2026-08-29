static u16 vr_elephant_trunk_anim_breathe_values[] = {
    0,
    0,84,163,235,297,346,380,397,397,380,346,297,235,163,84,0,
    (u16)-84,(u16)-163,(u16)-235,(u16)-297,(u16)-346,(u16)-380,(u16)-397,(u16)-397,(u16)-380,(u16)-346,(u16)-297,(u16)-235,(u16)-163,(u16)-84,
    0,(u16)-105,(u16)-204,(u16)-294,(u16)-371,(u16)-433,(u16)-475,(u16)-496,(u16)-496,(u16)-475,(u16)-433,(u16)-371,(u16)-294,(u16)-204,(u16)-105,0,
    105,204,294,371,433,475,496,496,475,433,371,294,204,105,
    0,63,122,176,223,260,285,298,298,285,260,223,176,122,63,0,
    (u16)-63,(u16)-122,(u16)-176,(u16)-223,(u16)-260,(u16)-285,(u16)-298,(u16)-298,(u16)-285,(u16)-260,(u16)-223,(u16)-176,(u16)-122,(u16)-63,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

void vr_elephant_trunk_set_joint_angles(
    s16 upperPitch,
    s16 middlePitch,
    s16 tipPitch,
    s16 upperYaw,
    s16 middleYaw,
    s16 tipYaw
) {
    // Each articulated section uses a thirty-sample channel. Updating all
    // samples keeps the pose deterministic while the renderer interpolates
    // the previous/current matrices at the headset refresh rate.
    for (u32 frame = 0; frame < 30; frame++) {
        vr_elephant_trunk_anim_breathe_values[1 + frame] = (u16)upperPitch;
        vr_elephant_trunk_anim_breathe_values[31 + frame] = (u16)middlePitch;
        vr_elephant_trunk_anim_breathe_values[61 + frame] = (u16)tipPitch;
        vr_elephant_trunk_anim_breathe_values[91 + frame] = (u16)upperYaw;
        vr_elephant_trunk_anim_breathe_values[121 + frame] = (u16)middleYaw;
        vr_elephant_trunk_anim_breathe_values[151 + frame] = (u16)tipYaw;
    }
}
static const u16 vr_elephant_trunk_anim_breathe_indices[] = {
    1,0,1,0,1,0, 1,0,1,0,1,0, 30,1,1,0,30,91,
    30,31,1,0,30,121, 30,61,1,0,30,151,
};
static const struct Animation vr_elephant_trunk_anim_breathe = {
    0,0,0,0,29,
    ANIMINDEX_NUMPARTS(vr_elephant_trunk_anim_breathe_indices),
    (u16 *)vr_elephant_trunk_anim_breathe_values,
    (u16 *)vr_elephant_trunk_anim_breathe_indices,
    0,
    ANIM_FIELD_LENGTH(vr_elephant_trunk_anim_breathe_values),
    ANIM_FIELD_LENGTH(vr_elephant_trunk_anim_breathe_indices),
};
