
# =========== Build =======================
echo "=== BUILD ==="

MUSESCORE_REVISION=$(git rev-parse --short=7 HEAD)

# Build portable AppImage
MUSE_APP_BUILD_MODE=$MUSE_APP_BUILD_MODE \
MUSESCORE_BUILD_CONFIGURATION="app-web" \
MUSESCORE_BUILD_NUMBER=$BUILD_NUMBER \
MUSESCORE_REVISION=$MUSESCORE_REVISION \
bash ./ninja_build.sh -t release

bash ./buildscripts/ci/tools/make_release_channel_env.sh -c $MUSE_APP_BUILD_MODE
bash ./buildscripts/ci/tools/make_version_env.sh $BUILD_NUMBER
bash ./buildscripts/ci/tools/make_revision_env.sh $MUSESCORE_REVISION
bash ./buildscripts/ci/tools/make_branch_env.sh

