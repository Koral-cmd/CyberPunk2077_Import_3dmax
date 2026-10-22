-- ============================================================
--  CP2077Import.mcr - the two windows of CP2077 Import.
--
--    CP2077 -> Import       character + weapon from the game
--    CP2077 -> Animations   step 2: one clip or all clips
--
--  All work happens in CP2077Import.dlu (MAXScript interface
--  CP77Cpp); these rollouts only collect the choices.
-- ============================================================

macroScript CP2077Import_Open
    category:"CP2077"
    buttonText:"CP2077 Import"
    toolTip:"Cyberpunk 2077 - import a character (and weapon) through WolvenKit"
(
    on execute do
    (
        local cp = undefined
        try (cp = CP77Cpp) catch (cp = undefined)
        if (cp == undefined) then
        (
            messageBox "CP2077Import.dlu is not loaded (CP77Cpp is missing).\nCheck the installation and the 3ds Max version." title:"CP2077 Import"
        )
        else
        (
            global cp77ImportRoll
            try (destroyDialog cp77ImportRoll) catch ()
            rollout cp77ImportRoll "CP2077 Import" width:540 height:820
            (
                local charPaths = #()
                local weapPaths = #()

                fn splitLines s = (filterString s "\n" splitEmptyTokens:false)
                fn isError s = (matchPattern s pattern:"ERROR*")
                fn report s = (
                    ::cp77ImportRoll.edReport.text = substituteString s "ERROR\t" "ERROR: "
                    windows.processPostedMessages()
                )
                fn busy what = (report ("Working: " + what + " ..."))
                -- dotNet list boxes: unlike "listbox" they scroll sideways for long paths
                fn styleList lb = (
                    lb.HorizontalScrollbar = true
                    lb.IntegralHeight = false
                    lb.BorderStyle = (dotNetClass "System.Windows.Forms.BorderStyle").FixedSingle
                    local cc = dotNetClass "System.Drawing.Color"
                    local bg = (colorMan.getColor #window) * 255
                    local fg = (colorMan.getColor #windowText) * 255
                    lb.BackColor = cc.FromArgb (bg[1] as integer) (bg[2] as integer) (bg[3] as integer)
                    lb.ForeColor = cc.FromArgb (fg[1] as integer) (fg[2] as integer) (fg[3] as integer)
                )
                fn setItems lb arr = (
                    lb.BeginUpdate()
                    lb.Items.Clear()
                    for t in arr do lb.Items.Add t
                    lb.EndUpdate()
                )
                fn selIndex lb = (lb.SelectedIndex + 1)
                fn selText lb arr = (
                    local i = lb.SelectedIndex + 1
                    if (i >= 1 and i <= arr.count) then arr[i] else undefined
                )

                group "Setup"
                (
                    edittext edGame "Game folder " fieldWidth:390 across:2
                    button btGame "..." width:26 align:#right offset:[0,-2]
                    edittext edCli "WolvenKit CLI" fieldWidth:390 across:2
                    button btCli "..." width:26 align:#right offset:[0,-2]
                    button btSave "Save settings" width:110 across:3
                    button btIndex "Build index" width:110 tooltip:"Lists every .ent and .anims of the game once (takes a while)"
                    label lbIndex "" width:170 align:#left
                )
                group "Character"
                (
                    edittext edCharFilter "Search" fieldWidth:390 across:2 text:"characters entities"
                    button btCharFind "Find" width:50 align:#right offset:[0,-2]
                    dotNetControl lbChars "System.Windows.Forms.ListBox" width:510 height:125
                    dropdownlist ddCharApp "Appearance" width:510
                )
                group "Weapon (optional)"
                (
                    checkbox cbWeapon "Attach a weapon" checked:false
                    edittext edWeapFilter "Search" fieldWidth:390 across:2 text:"w_"
                    button btWeapFind "Find" width:50 align:#right offset:[0,-2]
                    dotNetControl lbWeaps "System.Windows.Forms.ListBox" width:510 height:85
                    dropdownlist ddWeapApp "Appearance" width:510
                    edittext edSlot "Slot" text:"WeaponRight" fieldWidth:160
                )
                group "Options"
                (
                    spinner spLod "LOD (1 = full) " range:[1,8,1] type:#integer fieldWidth:40 across:3 align:#left tooltip:"2, 3 ... = lighter meshes, when the game has them (re-exports once)"
                    checkbox cbTex "Textures" checked:true tooltip:"Materials with the textures WolvenKit exported (multilayer colours are baked)"
                    checkbox cbFlipG "Flip normal green" checked:false
                    checkbox cbDecals "Decals (logos, stickers)" checked:true across:2 align:#left
                    dropdownlist ddMtl "" items:#("Physical Material", "Standard Material") width:150 align:#right
                )
                button btImport "Import" width:220 height:32
                button btJson "Import a WolvenKit project file (.ent.json) ..." width:300
                edittext edReport "" height:120 readOnly:true

                -- the "Show Shaded Material in Viewport" button for every imported textured material
                fn showMapsInViewport = (
                    for o in geometry where o.material != undefined do (
                        local m = o.material
                        local subs = (if classOf m == Multimaterial then m.materialList else #(m))
                        for s in subs where s != undefined do (
                            try (
                                if (hasProperty s #base_color_map) and s.base_color_map != undefined then showTextureMap s s.base_color_map true
                                else if (hasProperty s #diffuseMap) and s.diffuseMap != undefined then showTextureMap s s.diffuseMap true
                            ) catch ()
                        )
                    )
                )
                fn fillApps ent dd = (
                    dd.items = #()
                    if (ent == undefined or ent == "") then return ok
                    busy ("appearances of " + ent)
                    local r = CP77Cpp.appearances ent
                    if (isError r) then (report r; return ok)
                    local ls = splitLines r
                    local def = ""
                    local items = #()
                    for l in ls do (
                        local f = filterString l "\t" splitEmptyTokens:true
                        if (f.count == 2 and f[1] == "default") then def = f[2] else append items l
                    )
                    dd.items = items
                    local i = findItem items def
                    dd.selection = (if i > 0 then i else 1)
                    report (items.count as string + " appearance(s), default: " + def)
                )
                fn findPaths kind filter lb = (
                    local r = CP77Cpp.search kind filter 800
                    local res = splitLines r
                    -- no index yet: a full depot path typed into the search field works too
                    if (res.count == 0 and (matchPattern filter pattern:"*.ent")) then res = #(filter)
                    setItems lb res
                    res
                )

                on cp77ImportRoll open do (
                    edGame.text = CP77Cpp.getSetting "GamePath"
                    edCli.text = CP77Cpp.getSetting "WolvenKitCli"
                    local slotName = CP77Cpp.getSetting "WeaponSlot"
                    edSlot.text = (if slotName == "" then "WeaponRight" else slotName)
                    local lod = CP77Cpp.getSetting "Lod"
                    spLod.value = (if lod == "" then 1 else (lod as integer))
                    cbTex.checked = (CP77Cpp.getSetting "Textures") != "0"
                    cbFlipG.checked = (CP77Cpp.getSetting "NormalFlipGreen") == "1"
                    cbDecals.checked = (CP77Cpp.getSetting "Decals") != "0"
                    ddMtl.selection = (if (CP77Cpp.getSetting "MaterialType") == "Standard" then 2 else 1)
                    styleList lbChars
                    styleList lbWeaps
                    local n = CP77Cpp.indexCount()
                    lbIndex.text = (if n > 0 then (n as string + " paths indexed") else "no index yet")
                    report ("CP2077 Import " + CP77Cpp.version() + "\nSettings: " + CP77Cpp.settingsPath())
                )
                on btGame pressed do (
                    local p = getSavePath caption:"Cyberpunk 2077 folder (contains archive\\pc\\content)" initialDir:edGame.text
                    if p != undefined then edGame.text = p
                )
                on btCli pressed do (
                    local p = getOpenFileName caption:"WolvenKit.CLI.exe (or cp77tools.exe)" types:"WolvenKit CLI (*.exe)|*.exe|All files (*.*)|*.*|"
                    if p != undefined then edCli.text = p
                )
                on btSave pressed do (
                    CP77Cpp.setSetting "GamePath" edGame.text
                    CP77Cpp.setSetting "WolvenKitCli" edCli.text
                    CP77Cpp.setSetting "WeaponSlot" edSlot.text
                    report ("Saved to " + CP77Cpp.settingsPath())
                )
                on btIndex pressed do (
                    CP77Cpp.setSetting "GamePath" edGame.text
                    CP77Cpp.setSetting "WolvenKitCli" edCli.text
                    busy "indexing the game archives (one to a few minutes)"
                    local r = CP77Cpp.buildIndex()
                    report r
                    lbIndex.text = (CP77Cpp.indexCount() as string + " paths indexed")
                )
                on btCharFind pressed do (
                    charPaths = findPaths "character" edCharFilter.text lbChars
                    report (charPaths.count as string + " character entities")
                )
                on edCharFilter entered t do (
                    charPaths = findPaths "character" t lbChars
                    report (charPaths.count as string + " character entities")
                )
                on lbChars SelectedIndexChanged sender args do fillApps (selText lbChars charPaths) ddCharApp
                on btWeapFind pressed do (
                    weapPaths = findPaths "weapon" edWeapFilter.text lbWeaps
                    report (weapPaths.count as string + " weapon entities")
                )
                on edWeapFilter entered t do (
                    weapPaths = findPaths "weapon" t lbWeaps
                    report (weapPaths.count as string + " weapon entities")
                )
                on lbWeaps SelectedIndexChanged sender args do (
                    if (selIndex lbWeaps) > 0 then (
                        cbWeapon.checked = true
                        fillApps (selText lbWeaps weapPaths) ddWeapApp
                    )
                )
                on btImport pressed do (
                    local ent = selText lbChars charPaths
                    local wSel = selText lbWeaps weapPaths
                    local wEnt = (if cbWeapon.checked and wSel != undefined then wSel else "")
                    if ((ent == undefined or ent == "") and wEnt == "") then (report "Pick a character (and/or a weapon) first."; return ok)
                    if ent == undefined then ent = ""
                    local app = (if ddCharApp.selected != undefined then ddCharApp.selected else "")
                    local wApp = (if ddWeapApp.selected != undefined then ddWeapApp.selected else "")
                    CP77Cpp.setSetting "Lod" (spLod.value as string)
                    CP77Cpp.setSetting "Textures" (if cbTex.checked then "1" else "0")
                    CP77Cpp.setSetting "NormalFlipGreen" (if cbFlipG.checked then "1" else "0")
                    CP77Cpp.setSetting "Decals" (if cbDecals.checked then "1" else "0")
                    CP77Cpp.setSetting "MaterialType" (if ddMtl.selection == 2 then "Standard" else "Physical")
                    busy ("importing " + ent)
                    local r = CP77Cpp.importCharacter ent app wEnt wApp edSlot.text
                    report r
                    showMapsInViewport()
                    completeRedraw()
                )
                on btJson pressed do (
                    CP77Cpp.setSetting "Lod" (spLod.value as string)
                    CP77Cpp.setSetting "Textures" (if cbTex.checked then "1" else "0")
                    CP77Cpp.setSetting "NormalFlipGreen" (if cbFlipG.checked then "1" else "0")
                    CP77Cpp.setSetting "Decals" (if cbDecals.checked then "1" else "0")
                    CP77Cpp.setSetting "MaterialType" (if ddMtl.selection == 2 then "Standard" else "Physical")
                    local p = getOpenFileName caption:"Entity exported by WolvenKit (.ent.json)" types:"Entity JSON (*.ent.json)|*.ent.json|JSON (*.json)|*.json|"
                    if p != undefined then (
                        busy ("importing " + p)
                        report (CP77Cpp.importJson p)
                        showMapsInViewport()
                        completeRedraw()
                    )
                )
            )
            createDialog cp77ImportRoll
        )
    )
)

macroScript CP2077Import_Anim
    category:"CP2077"
    buttonText:"CP2077 Animations"
    toolTip:"Cyberpunk 2077 - load animations onto an imported skeleton (one clip or all)"
(
    on execute do
    (
        local cp = undefined
        try (cp = CP77Cpp) catch (cp = undefined)
        if (cp == undefined) then
        (
            messageBox "CP2077Import.dlu is not loaded (CP77Cpp is missing)." title:"CP2077 Animations"
        )
        else
        (
            global cp77AnimRoll
            try (destroyDialog cp77AnimRoll) catch ()
            rollout cp77AnimRoll "CP2077 Animations" width:540 height:900
            (
                local rigs = #()      -- #(handle, name, owner, entity, #(animsets))
                local clipNames = #()
                local currentSet = ""

                fn splitLines s = (filterString s "\n" splitEmptyTokens:false)
                fn isError s = (matchPattern s pattern:"ERROR*")
                fn report s = (
                    ::cp77AnimRoll.edReport.text = substituteString s "ERROR\t" "ERROR: "
                    windows.processPostedMessages()
                )
                fn busy what = (report ("Working: " + what + " ..."))
                local setPaths = #()
                -- dotNet list boxes: unlike "listbox" they scroll sideways for long paths
                fn styleList lb = (
                    lb.HorizontalScrollbar = true
                    lb.IntegralHeight = false
                    lb.BorderStyle = (dotNetClass "System.Windows.Forms.BorderStyle").FixedSingle
                    local cc = dotNetClass "System.Drawing.Color"
                    local bg = (colorMan.getColor #window) * 255
                    local fg = (colorMan.getColor #windowText) * 255
                    lb.BackColor = cc.FromArgb (bg[1] as integer) (bg[2] as integer) (bg[3] as integer)
                    lb.ForeColor = cc.FromArgb (fg[1] as integer) (fg[2] as integer) (fg[3] as integer)
                )
                fn setItems lb arr = (
                    lb.BeginUpdate()
                    lb.Items.Clear()
                    for t in arr do lb.Items.Add t
                    lb.EndUpdate()
                )
                fn selIndex lb = (lb.SelectedIndex + 1)
                fn selText lb arr = (
                    local i = lb.SelectedIndex + 1
                    if (i >= 1 and i <= arr.count) then arr[i] else undefined
                )

                group "Skeleton"
                (
                    dropdownlist ddRig "" width:430 across:2
                    button btRigs "Refresh" width:70 align:#right
                )
                group "Animset"
                (
                    radiobuttons rbSrc labels:#("Animsets of this skeleton", "Search all animsets") default:1 columns:2
                    edittext edFilter "Search" fieldWidth:390 across:2
                    button btFind "Find" width:50 align:#right offset:[0,-2]
                    dotNetControl lbSets "System.Windows.Forms.ListBox" width:510 height:100
                    button btLocal "Use a local .anims.glb ..." width:200
                )
                group "Clips"
                (
                    button btClips "List clips" width:120 across:2
                    label lbInfo "" align:#left width:360
                    dotNetControl lbClips "System.Windows.Forms.ListBox" width:510 height:170
                    button btOne "Add selected clip" width:160 across:3 tooltip:"Added after the clips already loaded (the first one starts at the time slider, rest pose before it). Double-click a clip does the same"
                    edittext edClipFilter "Filter" fieldWidth:110
                    button btAll "Load all (filtered)" width:160 tooltip:"Replaces everything: all clips one after another"
                    spinner spGap "Gap between clips (frames)" range:[0,1000,10] type:#integer fieldWidth:50 across:2 align:#left
                    button btReset "Back to rest pose" width:140 align:#right tooltip:"Removes every loaded clip"
                )
                group "Loaded clips (double-click = play only that clip)"
                (
                    dotNetControl lbLoaded "System.Windows.Forms.ListBox" width:510 height:110
                    button btPreview "Play selected" width:120 across:3
                    button btShowAll "Whole timeline" width:120
                    button btDelete "Delete selected" width:120 tooltip:"Removes that clip's keys; the others stay where they are"
                )
                edittext edReport "" height:100 readOnly:true

                fn rigHandle = (
                    if (ddRig.selection < 1 or ddRig.selection > rigs.count) then 0 else rigs[ddRig.selection][1]
                )
                local loadedClips = #()   -- #(name, startFrame, endFrame)
                fn refreshLoaded = (
                    loadedClips = #()
                    local items = #()
                    local r = CP77Cpp.listLoaded (rigHandle())
                    if not (isError r) then (
                        for l in (splitLines r) do (
                            local f = filterString l "\t" splitEmptyTokens:true
                            if f.count >= 3 then (
                                append loadedClips #(f[1], (f[2] as integer), (f[3] as integer))
                                append items (f[1] + "    [" + f[2] + " - " + f[3] + "]")
                            )
                        )
                    )
                    setItems lbLoaded items
                )
                fn showSets = (
                    if (rbSrc.state == 1) then (
                        setPaths = (if (ddRig.selection >= 1 and ddRig.selection <= rigs.count) then rigs[ddRig.selection][5] else #())
                    ) else (
                        setPaths = splitLines (CP77Cpp.search "anims" edFilter.text 800)
                    )
                    setItems lbSets setPaths
                )
                fn loadRigs = (
                    rigs = #()
                    local items = #()
                    for l in (splitLines (CP77Cpp.listRigs())) do (
                        local f = filterString l "\t" splitEmptyTokens:true
                        if f.count >= 4 then (
                            local sets = (if f.count >= 5 then filterString f[5] ";" splitEmptyTokens:false else #())
                            append rigs #((f[1] as integer), f[2], f[3], f[4], sets)
                            append items (f[2] + "   [" + f[3] + "]")
                        )
                    )
                    ddRig.items = items
                    if items.count > 0 then ddRig.selection = 1
                    showSets()
                    refreshLoaded()
                    report (if rigs.count == 0 then "No imported skeleton in the scene - use CP2077 Import first." else (rigs.count as string + " skeleton(s) in the scene"))
                )
                fn listClips setPath = (
                    if (setPath == undefined or setPath == "") then (report "Pick an animset first."; return ok)
                    busy ("reading " + setPath)
                    local r = CP77Cpp.clips setPath
                    if (isError r) then (report r; return ok)
                    currentSet = setPath
                    clipNames = #()
                    local items = #()
                    local total = 0.0
                    for l in (splitLines r) do (
                        local f = filterString l "\t" splitEmptyTokens:true
                        append clipNames f[1]
                        local d = (if f.count > 1 then (f[2] as float) else 0.0)
                        total += d
                        append items (f[1] + "    (" + (formattedPrint d format:".2f") + " s)")
                    )
                    setItems lbClips items
                    lbInfo.text = (clipNames.count as string + " clips, " + (formattedPrint total format:".1f") + " s in total")
                    report ("Animset " + setPath)
                )
                -- WhiteoutDex keeps sequences in custom attributes on the scene root: rebuild
                -- them from the root note track the plugin just wrote
                fn syncWhiteoutDex = (
                    if (globalVars.isGlobal #WdxSequenceStorage) and (WdxSequenceStorage != undefined) then (
                        try (
                            local n = 0
                            if (numNoteTracks rootNode) == 0 then WdxSequenceStorage.clearCA()
                            else n = WdxSequenceStorage.migrateFromNoteTracks removeTracks:true
                            report (edReport.text + "\nWhiteoutDex: " + (n as string) + " sequence(s)")
                        ) catch (report (edReport.text + "\nWhiteoutDex sequences not updated: " + getCurrentException()))
                    )
                )
                fn playLoaded = (
                    local i = selIndex lbLoaded
                    if (i < 1 or i > loadedClips.count) then (report "Pick a loaded clip."; return ok)
                    local c = loadedClips[i]
                    animationRange = interval c[2] (amax (c[2] + 1) c[3])
                    sliderTime = c[2]
                    playAnimation immediateReturn:true
                )
                fn applyOne = (
                    local i = selIndex lbClips
                    if (currentSet == "" or i < 1 or i > clipNames.count) then (report "List the clips and pick one."; return ok)
                    busy ("loading " + clipNames[i])
                    report (CP77Cpp.applyClip currentSet clipNames[i] (rigHandle()))
                    syncWhiteoutDex()
                    refreshLoaded()
                    completeRedraw()
                )

                on cp77AnimRoll open do (
                    styleList lbSets
                    styleList lbClips
                    styleList lbLoaded
                    local g = CP77Cpp.getSetting "GapFrames"
                    if g != "" then spGap.value = (g as integer)
                    loadRigs()
                )
                on btRigs pressed do loadRigs()
                on ddRig selected i do (showSets(); refreshLoaded())
                on rbSrc changed s do showSets()
                on btFind pressed do (rbSrc.state = 2; showSets())
                on edFilter entered t do (rbSrc.state = 2; showSets())
                on lbSets DoubleClick sender args do listClips (selText lbSets setPaths)
                on btClips pressed do listClips (selText lbSets setPaths)
                on btLocal pressed do (
                    local p = getOpenFileName caption:"Animset exported by WolvenKit (.anims.glb)" types:"Animset GLB (*.glb)|*.glb|"
                    if p != undefined then listClips p
                )
                on btOne pressed do applyOne()
                on lbClips DoubleClick sender args do applyOne()
                on btAll pressed do (
                    if currentSet == "" then (report "List the clips first."; return ok)
                    CP77Cpp.setSetting "GapFrames" (spGap.value as string)
                    busy "loading all clips"
                    report (CP77Cpp.applyAll currentSet edClipFilter.text (rigHandle()))
                    syncWhiteoutDex()
                    refreshLoaded()
                    completeRedraw()
                )
                on btReset pressed do (
                    report (CP77Cpp.resetPose (rigHandle()))
                    syncWhiteoutDex()
                    refreshLoaded()
                    completeRedraw()
                )
                on lbLoaded DoubleClick sender args do playLoaded()
                on btPreview pressed do playLoaded()
                on btShowAll pressed do (
                    stopAnimation()
                    local e = 0
                    for c in loadedClips do e = amax e c[3]
                    animationRange = interval 0 (amax 1 e)
                )
                on btDelete pressed do (
                    local i = selIndex lbLoaded
                    if (i < 1 or i > loadedClips.count) then (report "Pick a loaded clip."; return ok)
                    stopAnimation()
                    report (CP77Cpp.deleteLoaded (rigHandle()) (i - 1))
                    syncWhiteoutDex()
                    refreshLoaded()
                    completeRedraw()
                )
            )
            createDialog cp77AnimRoll
        )
    )
)
