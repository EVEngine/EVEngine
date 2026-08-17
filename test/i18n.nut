function basic() {
    if (eve.I18n == null) {
        print("can not find I18n");
        return false;
    }
    local p = eve.I18n();
    if (p.getName() != "I18n") {
        print("i18n name is not right: " + p.getName());
        return false;
    }

    // loadFromJson with nested keys and unicode escapes.
    if (!p.loadFromJson("en", @"{
      ""menu"": { ""start"": ""Start"", ""quit"": ""Quit"" },
      ""greeting"": ""Hello, {name}!"",
      ""items"": { ""one"": ""{n} item"", ""other"": ""{n} items"" }
    }")) {
        print("loadFromJson(en) failed");
        return false;
    }
    if (!p.loadFromJson("zh", @"{
      ""menu"": { ""start"": ""\u5f00\u59cb"", ""quit"": ""\u9000\u51fa"" },
      ""greeting"": ""\u4f60\u597d\uff0c{name}\uff01"",
      ""items"": { ""one"": ""{n} \u4e2a"", ""other"": ""{n} \u4e2a"" }
    }")) {
        print("loadFromJson(zh) failed");
        return false;
    }

    if (p.getLanguageCount() != 2) {
        print("language count != 2");
        return false;
    }
    if (!p.setLanguage("zh")) {
        print("setLanguage(zh) failed");
        return false;
    }
    if (p.get("menu.start") != "\u5f00\u59cb") {
        print("get menu.start failed: " + p.get("menu.start"));
        return false;
    }
    if (p.getWithParams("greeting", { name = "Alice" }) != "\u4f60\u597d\uff0cAlice\uff01") {
        print("greeting params failed: " + p.getWithParams("greeting", { name = "Alice" }));
        return false;
    }

    // Default-language fallback for missing key.
    p.setDefaultLanguage("en");
    p.setLanguage("fr");
    if (!p.setLanguage("en")) {
        print("setLanguage(en) failed");
        return false;
    }

    // Plurals.
    if (p.getPlural("items", 1) != "1 item") {
        print("plural one failed: " + p.getPlural("items", 1));
        return false;
    }
    if (p.getPlural("items", 5) != "5 items") {
        print("plural other failed: " + p.getPlural("items", 5));
        return false;
    }

    print("i18n script test OK");
    return true;
}
