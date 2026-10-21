-- ============================================================
--  CP2077Menu_2022_2024.ms - main menu "CP2077" for 3ds Max 2022-2024
--  (menuMan API; runs as a post-start-up script).
--  3ds Max 2025+ uses CP2077Menu_2025.ms instead.
-- ============================================================
(
    local menuName = "CP2077"
    local items = #(#("Import Character / Weapon", "CP2077Import_Open"), #("Animations", "CP2077Import_Anim"))
    local mnu = menuMan.findMenu menuName
    if (mnu == undefined) then
    (
        local bar = menuMan.getMainMenuBar()
        mnu = menuMan.createMenu menuName
        local sub = menuMan.createSubMenuItem menuName mnu
        bar.addItem sub (bar.numItems())
    )
    for it in items do
    (
        local found = false
        for i = 1 to mnu.numItems() while not found do
            if ((mnu.getItem i).getTitle() == it[1]) then found = true
        if not found then
        (
            local act = menuMan.createActionItem it[2] "CP2077"
            act.setTitle it[1]
            act.setUseCustomTitle true
            mnu.addItem act -1
        )
    )
    menuMan.updateMenuBar()
)
