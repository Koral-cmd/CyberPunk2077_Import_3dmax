-- ============================================================
--  CP2077Menu_2025.ms - main menu "CP2077" for 3ds Max 2025 and newer.
--
--  Since 2025 menus can only be created inside the #cuiRegisterMenus
--  callback, which must be registered BEFORE Max builds its menus -
--  this file therefore runs as a PRE-start-up script.
--  647394 is the action table of macroScripts; the permanent id is
--  "<macro name>`<category>".  The GUIDs belong to this package only.
-- ============================================================
(
    fn cp77RegisterMenu =
    (
        local mgr = callbacks.notificationParam()
        if (mgr != undefined) then
        (
            try
            (
                local mnu = mgr.mainMenuBar.CreateSubMenu "FD52268C-4FE5-45B8-9E76-38292E5DF45A" "CP2077"
                if (mnu != undefined) then
                (
                    mnu.CreateAction "FAA68F43-31C8-4722-8E65-8372C91308D7" 647394 "CP2077Import_Open`CP2077"
                    mnu.CreateAction "6DE4DF99-0381-49AA-9510-C2D7213496D3" 647394 "CP2077Import_Anim`CP2077"
                )
            )
            catch (format "CP2077 menu: %\n" (getCurrentException()))
        )
    )
    callbacks.removeScripts id:#cp77ImportMenu
    callbacks.addScript #cuiRegisterMenus cp77RegisterMenu id:#cp77ImportMenu
)
