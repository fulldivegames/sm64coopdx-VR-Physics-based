$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    function FunctionText($path,$name) {
        $s=Get-Content $path -Raw
        $f=[regex]::Match($s,"(?ms)^(?:static )?(?:bool|void) $name\(.*?^\}").Value
        if(!$f){throw "Missing production function $name"}; return $f
    }
    $functions=(FunctionText src/pc/vr/vr.c vr_gameplay_modifiers_allowed)+"`n"+
        (FunctionText src/game/vr_hand_interaction.c vr_hand_interaction_update_punch_sound)+"`n"+
        (FunctionText src/game/mario.c vr_turn_toward_headset_on_jump_landing)
    [IO.File]::WriteAllText((Join-Path $root 'build/test_vr_speedrun_options.inc'),$functions)
    & 'C:\msys64\mingw64\bin\gcc.exe' -std=gnu11 -O2 tests/test_vr_speedrun_options.c -o build/test_vr_speedrun_options.exe
    if($LASTEXITCODE){throw 'Compile failed'}
    & ./build/test_vr_speedrun_options.exe
    if($LASTEXITCODE){throw 'Runtime tests failed'}
    $config=Get-Content src/pc/configfile.c -Raw
    foreach($option in @('DisablePunchSound','SpeedRunningMode','VanillaMovement')){
        if($config -notmatch "bool\s+configVr$option\s*=\s*false;" -or $config -notmatch "\.boolValue = &configVr$option") {throw "Config default/storage missing $option"}
    }
    $menu=Get-Content src/pc/djui/djui_panel_vr.c -Raw
    if($menu -notmatch '(?s)"Physical Jumping"[^;]+;\s*djui_checkbox_create\(body, "Disable Punch Sound"'){throw 'Punch option misplaced'}
    if($menu -notmatch '(?s)djui_panel_menu_create\("Speedrunning", false\);\s*struct DjuiBase\* body[^;]+;\s*djui_checkbox_create\(body, "Speed Running Mode \(Vanilla Game\)"[^;]+;\s*djui_checkbox_create\(body, "Vanilla Movement \(Restrictive Jumps\)"'){throw 'Speedrunning options misplaced'}
    foreach($file in @('src/game/vr_hand_interaction.c','src/game/mario_actions_airborne.c','src/game/mario_actions_moving.c','src/game/mario_actions_submerged.c','src/game/rendering_graph_node.c','src/pc/djui/djui_panel_vr.c')){
        $s=Get-Content $file -Raw
        if($s.Contains('ns_coopnet_vr_gameplay_allowed()')){throw "Ungated VR gameplay path: $file"}
    }
    $policy=FunctionText src/game/vr_hand_interaction.c vr_special_moves_online_allowed
    if(!$policy.Contains('vr_gameplay_modifiers_allowed()')){throw 'Power-up policy not overridden'}
    $voices=Get-Content src/game/characters.c -Raw
    if($voices.Contains('configVrDisablePunchSound')){throw 'Mute leaked into shared jump/kick voice dispatch'}
    $voiceChat=Get-Content src/pc/network/voice_chat.c -Raw
    if($voiceChat.Contains('vr_gameplay_modifiers_allowed')){throw 'Gameplay mode must not disable voice chat'}
    'PASS: menu order, persistence, all VR cheat/speed/spawn gates, separate network/voice policy and jump/kick dispatch'
} finally {Pop-Location}
