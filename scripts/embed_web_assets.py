from pathlib import Path

Import("env")


project_dir = Path(env.subst("$PROJECT_DIR"))
web_dir = project_dir / "web"
generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_header = generated_dir / "ui_assets.h"


def read_asset(name: str) -> str:
    return (web_dir / name).read_text(encoding="utf-8").strip()


def inline_page(template_name: str, css_name: str, js_name: str | None = None) -> str:
    page = read_asset(template_name)
    page = page.replace("{{STYLE}}", f"<style>\n{read_asset(css_name)}\n</style>")
    if js_name is not None:
        page = page.replace("{{SCRIPT}}", f"<script>\n{read_asset(js_name)}\n</script>")
    return page


def raw_string(name: str, content: str) -> str:
    terminator = ")UI_ASSET\""
    if terminator in content:
        raise ValueError(f"{name} contains the reserved raw-string terminator")
    return f'inline constexpr char {name}[] = R"UI_ASSET({content})UI_ASSET";'


def split_template(content: str, marker: str) -> tuple[str, str]:
    parts = content.split(marker)
    if len(parts) != 2:
        raise ValueError(f"expected exactly one {marker} marker")
    return parts[0], parts[1]


index_page = inline_page("index.html", "index.css", "index.js")
onboarding_page = inline_page("onboarding.html", "onboarding.css", "onboarding.js")
saved_page = inline_page("message-saved.html", "message.css")
error_page = inline_page("message-error.html", "message.css")

onboarding_before, onboarding_after = split_template(
    onboarding_page, "{{ACCESS_POINT}}"
)
saved_before, saved_after = split_template(saved_page, "{{SSID}}")
error_before, error_after = split_template(error_page, "{{ERROR}}")

declarations = [
    raw_string("kIndexHtml", index_page),
    raw_string("kOnboardingHtmlBeforeAccessPoint", onboarding_before),
    raw_string("kOnboardingHtmlAfterAccessPoint", onboarding_after),
    raw_string("kOnboardingSavedHtmlBefore", saved_before),
    raw_string("kOnboardingSavedHtmlAfter", saved_after),
    raw_string("kOnboardingErrorHtmlBefore", error_before),
    raw_string("kOnboardingErrorHtmlAfter", error_after),
]

header = (
    "#pragma once\n\n"
    "namespace app::ui {\n\n"
    + "\n\n".join(declarations)
    + "\n\n}  // namespace app::ui\n"
)

generated_dir.mkdir(parents=True, exist_ok=True)
if not generated_header.exists() or generated_header.read_text(encoding="utf-8") != header:
    generated_header.write_text(header, encoding="utf-8")

env.Append(CPPPATH=[str(generated_dir)])
