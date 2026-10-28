#include "animation/AnimImporter.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <fstream>
#include <sstream>

namespace eve::animation {

namespace {

std::string trim(const std::string &s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string AnimImporter::exportAnimationFixtureText(const AnimSkeleton *skeleton, const AnimClip *clip) {
    if (!skeleton || !clip) throw Exception("AnimImporter.exportAnimationFixtureText: null argument");
    std::ostringstream out;
    out << "EVA 1\n";
    out << "skeleton " << skeleton->getBoneCount() << "\n";
    for (int i = 0; i < skeleton->getBoneCount(); ++i) {
        out << "bone " << i << " " << skeleton->getParent(i) << " " << skeleton->getBoneName(i)
            << "\n";
        out << "bind " << i << " " << skeleton->getBindPositionX(i) << " "
            << skeleton->getBindPositionY(i) << " " << skeleton->getBindPositionZ(i) << " "
            << skeleton->getBindRotationX(i) << " " << skeleton->getBindRotationY(i) << " "
            << skeleton->getBindRotationZ(i) << " " << skeleton->getBindRotationW(i) << " "
            << skeleton->getBindScaleX(i) << " " << skeleton->getBindScaleY(i) << " "
            << skeleton->getBindScaleZ(i) << "\n";
    }
    out << "clip " << clip->getName() << "\n";
    out << "duration " << clip->getDuration() << "\n";
    out << "loop " << (clip->getLoop() ? 1 : 0) << "\n";
    out << "rate " << clip->getSampleRate() << "\n";
    for (int i = 0; i < clip->getSyncMarkerCount(); ++i)
        out << "sync " << clip->getSyncMarkerTime(i) << " " << clip->getSyncMarkerName(i) << "\n";
    for (int b = 0; b < skeleton->getBoneCount(); ++b) {
        const int np = clip->getPositionKeyCount(b);
        const int nr = clip->getRotationKeyCount(b);
        const int ns = clip->getScaleKeyCount(b);
        if (np + nr + ns == 0) continue;
        out << "track " << b << " " << np << " " << nr << " " << ns << "\n";
        for (int i = 0; i < np; ++i) {
            out << "p " << clip->getPositionKeyTime(b, i) << " " << clip->getPositionKeyX(b, i)
                << " " << clip->getPositionKeyY(b, i) << " " << clip->getPositionKeyZ(b, i) << "\n";
        }
        for (int i = 0; i < nr; ++i) {
            out << "r " << clip->getRotationKeyTime(b, i) << " " << clip->getRotationKeyX(b, i)
                << " " << clip->getRotationKeyY(b, i) << " " << clip->getRotationKeyZ(b, i) << " "
                << clip->getRotationKeyW(b, i) << "\n";
        }
        for (int i = 0; i < ns; ++i) {
            out << "s " << clip->getScaleKeyTime(b, i) << " " << clip->getScaleKeyX(b, i) << " "
                << clip->getScaleKeyY(b, i) << " " << clip->getScaleKeyZ(b, i) << "\n";
        }
    }
    out << "end\n";
    return out.str();
}

void AnimImporter::importAnimationFixtureText(const std::string &text, AnimSkeleton **skeletonOut,
                                              AnimClip **clipOut) {
    if (!skeletonOut || !clipOut) throw Exception("AnimImporter.importAnimationFixtureText: null out");
    *skeletonOut = nullptr;
    *clipOut     = nullptr;

    auto *skeleton = new AnimSkeleton();
    AnimClip *clip = nullptr;
    int curBone    = -1;

    std::istringstream in(text);
    std::string line;
    try {
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string tag;
            ls >> tag;
            if (tag == "EVA") {
                int ver = 0;
                ls >> ver;
                if (ver != 1) throw Exception("AnimImporter.importAnimationFixtureText: unsupported version");
            } else if (tag == "skeleton") {
                // informational
            } else if (tag == "bone") {
                int idx = -1, parent = -1;
                std::string name;
                ls >> idx >> parent;
                std::getline(ls, name);
                name = trim(name);
                if (name.empty()) throw Exception("AnimImporter.importAnimationFixtureText: empty bone name");
                const int id = skeleton->addBone(name, parent);
                if (id != idx) {
                    throw Exception("AnimImporter.importAnimationFixtureText: bone index mismatch");
                }
            } else if (tag == "bind") {
                int idx = -1;
                float px, py, pz, qx, qy, qz, qw, sx, sy, sz;
                ls >> idx >> px >> py >> pz >> qx >> qy >> qz >> qw >> sx >> sy >> sz;
                skeleton->setBindPosition(idx, px, py, pz);
                skeleton->setBindRotation(idx, qx, qy, qz, qw);
                skeleton->setBindScale(idx, sx, sy, sz);
            } else if (tag == "clip") {
                std::string name;
                std::getline(ls, name);
                name = trim(name);
                clip = new AnimClip(name.empty() ? "clip" : name);
            } else if (tag == "duration") {
                if (!clip) throw Exception("AnimImporter.importAnimationFixtureText: duration before clip");
                float d = 0.f;
                ls >> d;
                clip->setDuration(d);
            } else if (tag == "loop") {
                if (!clip) throw Exception("AnimImporter.importAnimationFixtureText: loop before clip");
                int v = 1;
                ls >> v;
                clip->setLoop(v != 0);
            } else if (tag == "rate") {
                if (!clip) throw Exception("AnimImporter.importAnimationFixtureText: rate before clip");
                float r = 30.f;
                ls >> r;
                clip->setSampleRate(r);
            } else if (tag == "sync") {
                if (!clip) throw Exception("AnimImporter.importAnimationFixtureText: sync marker before clip");
                float t = 0.f;
                std::string name;
                ls >> t;
                std::getline(ls, name);
                name = trim(name);
                clip->addSyncMarker(t, name);
            } else if (tag == "track") {
                if (!clip) throw Exception("AnimImporter.importAnimationFixtureText: track before clip");
                int np = 0, nr = 0, ns = 0;
                ls >> curBone >> np >> nr >> ns;
                (void)np;
                (void)nr;
                (void)ns;
            } else if (tag == "p") {
                float t, x, y, z;
                ls >> t >> x >> y >> z;
                clip->addPositionKey(curBone, t, x, y, z);
            } else if (tag == "r") {
                float t, x, y, z, w;
                ls >> t >> x >> y >> z >> w;
                clip->addRotationKey(curBone, t, x, y, z, w);
            } else if (tag == "s") {
                float t, x, y, z;
                ls >> t >> x >> y >> z;
                clip->addScaleKey(curBone, t, x, y, z);
            } else if (tag == "end") {
                break;
            }
        }
        if (!clip) throw Exception("AnimImporter.importAnimationFixtureText: missing clip");
        *skeletonOut = skeleton;
        *clipOut     = clip;
    } catch (...) {
        delete skeleton;
        delete clip;
        throw;
    }
}

void AnimImporter::importAnimationFixtureTextFile(const std::string &path, AnimSkeleton **skeletonOut,
                                                  AnimClip **clipOut) {
    std::ifstream in(path);
    if (!in) throw Exception("AnimImporter.importAnimationFixtureTextFile: cannot open %s", path.c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    importAnimationFixtureText(ss.str(), skeletonOut, clipOut);
}

}  // namespace eve::animation
