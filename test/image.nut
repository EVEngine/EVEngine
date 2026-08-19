function basic() {
    if (eve.Image == null) {
        print("can not find Image");
        return false;
    }

    local p = eve.Image();
    if (p.getName() != "Image") {
        print("Image name is not right: ");
        print(p.getName());
        return false;
    }
    return true;
}

function imageDataClass() {
    // eve.ImageData is the single script class shared by Image, Font and Model3D.
    if (eve.ImageData == null) {
        print("can not find ImageData class");
        return false;
    }
    return true;
}

function newImageData() {
    local img = eve.Image();
    local data = img.newEmptyImageData(4, 3, "RGBA8");
    if (data == null) return false;
    if (data.getWidth() != 4 || data.getHeight() != 3) return false;
    if (data.getFormat() != "RGBA8") return false;
    if (data.getSize() != 4 * 3 * 4) return false;
    if (!data.inside(0, 0) || data.inside(4, 0)) return false;
    return true;
}

function pixelRoundTrip() {
    local img = eve.Image();
    local data = img.newEmptyImageData(2, 2, "RGBA8");
    data.setPixel(1, 0, 0.25, 0.5, 0.75, 1.0);
    if (data.getPixelR(1, 0) < 0.24 || data.getPixelR(1, 0) > 0.26) return false;
    if (data.getPixelG(1, 0) < 0.49 || data.getPixelG(1, 0) > 0.51) return false;
    if (data.getPixelB(1, 0) < 0.74 || data.getPixelB(1, 0) > 0.76) return false;
    if (data.getPixelA(1, 0) < 0.99) return false;
    return true;
}

function cloneAndRotate() {
    local img = eve.Image();
    local data = img.newEmptyImageData(3, 2, "RGBA8");
    data.setPixel(1, 0, 1.0, 0.0, 0.0, 1.0);

    local copy = data.clone();
    if (copy == null) return false;
    if (copy.getWidth() != 3 || copy.getHeight() != 2) return false;
    if (copy.getPixelR(1, 0) < 0.99) return false;

    local rot = data.rotate(1.5707963, "nearest", true);
    if (rot == null) return false;
    if (rot.getWidth() < 1 || rot.getHeight() < 1) return false;
    return true;
}

function paste() {
    local img = eve.Image();
    local src = img.newEmptyImageData(2, 1, "RGBA8");
    local dst = img.newEmptyImageData(4, 2, "RGBA8");
    src.setPixel(1, 0, 1.0, 0.0, 0.0, 1.0);
    dst.paste(src, 2, 0, 0, 0, 2, 1);
    // src(1,0) is red and lands at dst(3,0); src(0,0) is transparent black at dst(2,0).
    if (dst.getPixelR(3, 0) < 0.99) return false;
    if (dst.getPixelR(2, 0) > 0.01) return false;
    return true;
}
