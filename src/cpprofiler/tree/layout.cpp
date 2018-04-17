#include "layout.hh"
#include "shape.hh"


#include <QDebug>
#include <iostream>

#include "../utils/std_ext.hh"

namespace cpprofiler { namespace tree {

    const Shape& Layout::getShape(NodeID nid) const {

        return *m_shapes[nid];
    }

    void Layout::setShape(NodeID nid, std::unique_ptr<Shape, ShapeDeleter> shape) {
        if (m_shapes.size() <= nid) {
            m_shapes.resize(static_cast<int>(nid)+1);
        }

        m_shapes[nid] = std::move(shape);
    }

    utils::Mutex& Layout::getMutex() const {
        return m_layout_mutex;
    }

    void Layout::setChildOffset(NodeID nid, double offset) {

        if (m_child_offsets.size() <= nid) {
            m_child_offsets.resize(static_cast<int>(nid)+1);
        }

        m_child_offsets[nid] = offset;
    }


    void Layout::setLayoutDone(NodeID nid, bool val) {

        if (m_layout_done.size() <= nid) {
            m_layout_done.resize(static_cast<int>(nid)+1);
        }

        m_layout_done[nid] = val;
    }

    bool Layout::getLayoutDone(NodeID nid) const {

        if (m_layout_done.size() <= nid) {
            return false;
        }

        return m_layout_done.at(nid);
    }

    Layout::Layout() {
    }

    Layout::~Layout() = default;

    double Layout::getOffset(NodeID nid) const {
        if (m_child_offsets.size() <= nid) {
            return 0;
        }

        return m_child_offsets[nid];
    }


    int Layout::getDepth(NodeID nid) const {
        return getShape(nid).depth();
    }

    const BoundingBox& Layout::getBoundingBox(NodeID nid) const {
        return getShape(nid).boundingBox();
    }

    void Layout::growDataStructures(int n_nodes) {
        // utils::MutexLocker locker(&m_layout_mutex);
        auto old_size = m_child_offsets.size();
        m_child_offsets.resize(old_size + n_nodes);
    }

    bool Layout::isDirty(NodeID nid) const {

        // qDebug() << "is Dirty";

        // for (auto el : m_dirty) {
        //     std::cerr << el << " ";
        // }
        // std::cerr << "\n";

        if (m_dirty.size() <= nid) {
            return true;
        }

        return m_dirty[nid];
    }

    void Layout::setDirty(NodeID nid, bool val) {

        if (m_dirty.size() <= nid) {
            m_dirty.resize(static_cast<int>(nid)+1, true);
        }

        m_dirty[nid] = val;
    }


}}

