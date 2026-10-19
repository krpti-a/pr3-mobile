#!/usr/bin/env python3
"""Generates PR3Remake.xcodeproj (no third-party tooling needed). Re-run after adding source files."""
import hashlib, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
APP = "PR3Remake"

def uid(name): return hashlib.sha1(name.encode()).hexdigest()[:24].upper()

# ---- source discovery -------------------------------------------------------------------------
sources = []     # (relative path, kind)
for sub in (APP, "PR3Core"):
    for base, _, files in os.walk(os.path.join(ROOT, sub)):
        if "Resources" in os.path.relpath(base, ROOT).split(os.sep): continue
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(base, f), ROOT)
            if f.endswith(".swift"): sources.append((rel, "swift"))
            elif f.endswith(".c"): sources.append((rel, "c"))
            elif f.endswith(".h"): sources.append((rel, "h"))
sources.sort()
compile_sources = [s for s in sources if s[1] in ("swift", "c")]
has_resources = os.path.isdir(os.path.join(ROOT, APP, "Resources", "GameData"))
has_assets = os.path.isdir(os.path.join(ROOT, APP, "Assets.xcassets"))

PROJECT, TARGET = uid("project"), uid("target")
MAIN_GROUP, PRODUCTS = uid("g_main"), uid("g_products")
PRODUCT_REF = uid("product")
RES_REF, RES_BUILD = uid("res_ref"), uid("res_build")
ASSET_REF, ASSET_BUILD = uid("asset_ref"), uid("asset_build")
SRC_PHASE, RES_PHASE, FW_PHASE = uid("p_src"), uid("p_res"), uid("p_fw")
PROJ_LIST, TARGET_LIST = uid("l_proj"), uid("l_target")
PROJ_DBG, PROJ_REL, TGT_DBG, TGT_REL = uid("c_pd"), uid("c_pr"), uid("c_td"), uid("c_tr")

file_ref = {rel: uid("ref_" + rel) for rel, _ in sources}
build_file = {rel: uid("bf_" + rel) for rel, k in compile_sources}
LANG = {"swift": "sourcecode.swift", "c": "sourcecode.c.c", "h": "sourcecode.c.h"}

# group tree
tree = {}
for rel, kind in sources:
    parts = rel.split(os.sep)
    node = tree
    for p in parts[:-1]: node = node.setdefault(p, {})
    node.setdefault("__files__", []).append((parts[-1], rel))
group_ids = {}
def gid(path): return group_ids.setdefault(path, uid("g_" + path))

out = ["// !$*UTF8*$!", "{", "\tarchiveVersion = 1;", "\tclasses = {", "\t};", "\tobjectVersion = 56;", "\tobjects = {", ""]
out.append("/* Begin PBXBuildFile section */")
for rel, k in compile_sources:
    flags = ""
    out.append(f"\t\t{build_file[rel]} = {{isa = PBXBuildFile; fileRef = {file_ref[rel]}; }};")
if has_resources: out.append(f"\t\t{RES_BUILD} = {{isa = PBXBuildFile; fileRef = {RES_REF}; }};")
if has_assets: out.append(f"\t\t{ASSET_BUILD} = {{isa = PBXBuildFile; fileRef = {ASSET_REF}; }};")
out.append("/* End PBXBuildFile section */\n")
out.append("/* Begin PBXFileReference section */")
for rel, k in sources:
    out.append(f"\t\t{file_ref[rel]} = {{isa = PBXFileReference; lastKnownFileType = {LANG[k]}; path = {os.path.basename(rel)}; sourceTree = \"<group>\"; }};")
if has_resources:
    out.append(f"\t\t{RES_REF} = {{isa = PBXFileReference; lastKnownFileType = folder; name = GameData; path = {APP}/Resources/GameData; sourceTree = \"<group>\"; }};")
if has_assets:
    out.append(f"\t\t{ASSET_REF} = {{isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; name = Assets.xcassets; path = {APP}/Assets.xcassets; sourceTree = \"<group>\"; }};")
out.append(f"\t\t{PRODUCT_REF} = {{isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = {APP}.app; sourceTree = BUILT_PRODUCTS_DIR; }};")
out.append("/* End PBXFileReference section */\n")
out.append("/* Begin PBXFrameworksBuildPhase section */")
out.append(f"\t\t{FW_PHASE} = {{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0; }};")
out.append("/* End PBXFrameworksBuildPhase section */\n")

out.append("/* Begin PBXGroup section */")
def emit_group(node, path):
    kids = []
    for name in sorted(k for k in node if k != "__files__"):
        kids.append(gid(path + "/" + name))
    for name, rel in node.get("__files__", []): kids.append(file_ref[rel])
    return kids
def walk(node, path, name):
    for sub in sorted(k for k in node if k != "__files__"):
        walk(node[sub], path + "/" + sub, sub)
    kids = emit_group(node, path)
    if path == "":
        kids = kids + ([RES_REF] if has_resources else []) + ([ASSET_REF] if has_assets else [])
        out.append(f"\t\t{MAIN_GROUP} = {{isa = PBXGroup; children = ({', '.join(kids + [PRODUCTS])}); sourceTree = \"<group>\"; }};")
    else:
        out.append(f"\t\t{gid(path)} = {{isa = PBXGroup; children = ({', '.join(kids)}); name = {name}; path = {name}; sourceTree = \"<group>\"; }};")
walk(tree, "", "")
out.append(f"\t\t{PRODUCTS} = {{isa = PBXGroup; children = ({PRODUCT_REF}); name = Products; sourceTree = \"<group>\"; }};")
out.append("/* End PBXGroup section */\n")

out.append("/* Begin PBXNativeTarget section */")
out.append(f"\t\t{TARGET} = {{isa = PBXNativeTarget; buildConfigurationList = {TARGET_LIST}; buildPhases = ({SRC_PHASE}, {FW_PHASE}, {RES_PHASE}); buildRules = (); dependencies = (); name = {APP}; productName = {APP}; productReference = {PRODUCT_REF}; productType = \"com.apple.product-type.application\"; }};")
out.append("/* End PBXNativeTarget section */\n")
out.append("/* Begin PBXProject section */")
out.append(f"\t\t{PROJECT} = {{isa = PBXProject; attributes = {{BuildIndependentTargetsInParallel = 1; LastSwiftUpdateCheck = 1500; LastUpgradeCheck = 1500; }}; buildConfigurationList = {PROJ_LIST}; compatibilityVersion = \"Xcode 14.0\"; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en, Base); mainGroup = {MAIN_GROUP}; productRefGroup = {PRODUCTS}; projectDirPath = \"\"; projectRoot = \"\"; targets = ({TARGET}); }};")
out.append("/* End PBXProject section */\n")
out.append("/* Begin PBXResourcesBuildPhase section */")
out.append(f"\t\t{RES_PHASE} = {{isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = ({', '.join(([RES_BUILD] if has_resources else []) + ([ASSET_BUILD] if has_assets else []))}); runOnlyForDeploymentPostprocessing = 0; }};")
out.append("/* End PBXResourcesBuildPhase section */\n")
out.append("/* Begin PBXSourcesBuildPhase section */")
out.append(f"\t\t{SRC_PHASE} = {{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({', '.join(build_file[r] for r, _ in compile_sources)}); runOnlyForDeploymentPostprocessing = 0; }};")
out.append("/* End PBXSourcesBuildPhase section */\n")

common = {
    "ALWAYS_SEARCH_USER_PATHS": "NO", "CLANG_ENABLE_MODULES": "YES", "CLANG_ENABLE_OBJC_ARC": "YES",
    "ENABLE_USER_SCRIPT_SANDBOXING": "NO", "GCC_C_LANGUAGE_STANDARD": "gnu17",
    "IPHONEOS_DEPLOYMENT_TARGET": "16.0", "SDKROOT": "iphoneos", "SWIFT_VERSION": "5.0",
    "GCC_OPTIMIZATION_LEVEL": "1", "CLANG_WARN_DEPRECATED_FUNCTIONS": "NO",
}
dbg = dict(common, **{"DEBUG_INFORMATION_FORMAT": "dwarf", "ENABLE_TESTABILITY": "YES", "ONLY_ACTIVE_ARCH": "YES",
                      "SWIFT_OPTIMIZATION_LEVEL": "-Onone", "SWIFT_ACTIVE_COMPILATION_CONDITIONS": "DEBUG"})
rel = dict(common, **{"DEBUG_INFORMATION_FORMAT": "dwarf-with-dsym", "SWIFT_OPTIMIZATION_LEVEL": "-O", "VALIDATE_PRODUCT": "YES"})
target = {
    "ASSETCATALOG_COMPILER_APPICON_NAME": "AppIcon",
    "CODE_SIGN_STYLE": "Automatic", "CURRENT_PROJECT_VERSION": "1", "GENERATE_INFOPLIST_FILE": "YES",
    "INFOPLIST_KEY_CFBundleDisplayName": "PR3",
    "INFOPLIST_KEY_UIApplicationSceneManifest_Generation": "YES", "INFOPLIST_KEY_UILaunchScreen_Generation": "YES",
    "INFOPLIST_KEY_UIRequiresFullScreen": "YES", "INFOPLIST_KEY_UIStatusBarHidden": "YES",
    "INFOPLIST_KEY_UISupportedInterfaceOrientations": "\"UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight\"",
    "INFOPLIST_KEY_UISupportedInterfaceOrientations_iPad": "\"UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight\"",
    "MARKETING_VERSION": "1.0", "PRODUCT_BUNDLE_IDENTIFIER": os.environ.get("PR3_BUNDLE_ID", "com.example.pr3remake"),
    "PRODUCT_NAME": "\"$(TARGET_NAME)\"", "SWIFT_EMIT_LOC_STRINGS": "YES", "TARGETED_DEVICE_FAMILY": "\"1,2\"",
    "SWIFT_OBJC_BRIDGING_HEADER": f"{APP}/PR3Remake-Bridging-Header.h",
    "HEADER_SEARCH_PATHS": "(\"$(SRCROOT)/PR3Core/include\", \"$(SRCROOT)/PR3Core/generated\")",
    "OTHER_CFLAGS": "(\"-w\", \"-ffp-contract=off\", \"-DRT_GL_REAL\", \"-DRT_AUDIO_REAL\")",
    "OTHER_LDFLAGS": "(\"-framework\", OpenGLES, \"-framework\", QuartzCore, \"-framework\", AudioToolbox, \"-framework\", OpenAL, \"-framework\", CoreFoundation)",
}
if os.environ.get("PR3_TEAM"): target["DEVELOPMENT_TEAM"] = os.environ["PR3_TEAM"]
def cfg(id_, name, settings):
    body = "; ".join(f"{k} = {v}" for k, v in sorted(settings.items()))
    return f"\t\t{id_} = {{isa = XCBuildConfiguration; buildSettings = {{{body}; }}; name = {name}; }};"
out.append("/* Begin XCBuildConfiguration section */")
out += [cfg(PROJ_DBG, "Debug", dbg), cfg(PROJ_REL, "Release", rel), cfg(TGT_DBG, "Debug", target), cfg(TGT_REL, "Release", target)]
out.append("/* End XCBuildConfiguration section */\n")
out.append("/* Begin XCConfigurationList section */")
out.append(f"\t\t{PROJ_LIST} = {{isa = XCConfigurationList; buildConfigurations = ({PROJ_DBG}, {PROJ_REL}); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }};")
out.append(f"\t\t{TARGET_LIST} = {{isa = XCConfigurationList; buildConfigurations = ({TGT_DBG}, {TGT_REL}); defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }};")
out.append("/* End XCConfigurationList section */")
out += ["\t};", f"\trootObject = {PROJECT};", "}", ""]
proj_dir = os.path.join(ROOT, f"{APP}.xcodeproj"); os.makedirs(proj_dir, exist_ok=True)
open(os.path.join(proj_dir, "project.pbxproj"), "w").write("\n".join(out))
print(f"Wrote {proj_dir}: {len(compile_sources)} sources, resources={has_resources}")
