// CUA REPL helpers. Bind nativeGame with cua.getApp(path) before loading.
// Run one named step from its stated screen, then inspect its checkpoint.
// Input goes through the actual macOS window and SDL keyboard bridge.
var meleeUi = {
    async tap(key, count = 1) {
        for (let i = 0; i < count; i++) await nativeGame.pressKey(key);
    },
    async checkpoint() { await nativeGame.getAXStateAndScreenshot(); },
    // Initial "Create Game Data?" dialog. This route tests without a save.
    async declineSave() { await this.tap('d', 3); await this.tap('x'); },
    // "Continue without saving" dialog.
    async openingToTitle() { await this.tap('x'); await this.tap('Return', 2); },
    // Title screen.
    async titleToMain() { await this.tap('Return'); },
    // Main menu, initial 1-P Mode selection.
    async mainToVs() { await this.tap('s'); await this.tap('x'); },
    // VS. Mode submenu, initial Melee selection.
    async vsToCharacters() { await this.tap('x'); },
    // Character screen, initial P1 glove. Movement is timing-sensitive under
    // OS key injection; inspect the result before confirming character/CPU.
    async moveToRoster() { await this.tap('w', 25); },
    async confirm() { await this.tap('x'); },
    // Character screen with human and CPU selected, READY TO FIGHT shown.
    async charactersToStages() { await this.tap('Return'); },
};
