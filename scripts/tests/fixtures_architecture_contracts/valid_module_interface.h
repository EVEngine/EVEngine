/**
 * @brief Cheap predicate used by fixture tests.
 * @cost O(1) local field read.
 */
bool isReady() const;

/**
 * @brief Materialize owned ids for a caller that takes ownership of the vector.
 * @cost O(n) allocation + copy of every id; prefer collectInto when hot.
 */
std::vector<std::string> collectIds();

class HotFrame {
public:
    void tick();
};
