#include "DataControl.hpp"

#include <algorithm>

#include "ext-data-control-v1.hpp"
#include "wlr-data-control-unstable-v1.hpp"
#include "wayland.hpp"

// Both protocols generate the same API under different names, so one implementation
// serves them: the template parameters are the generated classes.
template <class TManager, class TDevice, class TSource, class TOffer>
class CDataControl : public IDataControl {
  public:
    CDataControl(SP<TManager> manager, SP<CCWlSeat> seat) : m_manager(manager) {
        m_device = makeShared<TDevice>(m_manager->sendGetDataDevice(seat->resource()));

        m_device->setDataOffer([this](TDevice*, wl_proxy* proxy) { onDataOffer(proxy); });
        m_device->setSelection([this](TDevice*, wl_proxy* proxy) { onSelection(proxy); });
        m_device->setFinished([this](TDevice*) {
            if (m_onFinished)
                m_onFinished();
        });
    }

    ~CDataControl() override {
        for (const auto& offer : m_announced)
            offer->destroy();

        m_device->sendDestroy();
        m_manager->sendDestroy();
    }

    SP<IDataControlSource> createSource(SendFn onSend, CancelledFn onCancelled) override {
        return makeShared<CSource>(m_manager->sendCreateDataSource(), std::move(onSend), std::move(onCancelled));
    }

    void setSelection(IDataControlSource* source) override {
        m_device->sendSetSelection(source ? sc<CSource*>(source)->m_source.get() : nullptr);
    }

  private:
    class COffer : public IDataControlOffer {
      public:
        COffer(wl_proxy* proxy) : m_offer(makeShared<TOffer>(proxy)) {
            // The formats follow as events, before the selection that uses the offer.
            m_offer->setOffer([this](TOffer*, const char* mime) { m_mimeTypes.emplace_back(mime); });
        }

        void receive(const std::string& mimeType, int fd) override {
            m_offer->sendReceive(mimeType.c_str(), fd);
        }

        void destroy() override {
            m_offer->sendDestroy();
        }

        SP<TOffer> m_offer;
    };

    class CSource : public IDataControlSource {
      public:
        CSource(wl_proxy* proxy, SendFn onSend, CancelledFn onCancelled) : m_source(makeShared<TSource>(proxy)) {
            m_source->setSend([onSend = std::move(onSend)](TSource*, const char* mime, int32_t fd) { onSend(mime, fd); });
            m_source->setCancelled([this, onCancelled = std::move(onCancelled)](TSource*) { onCancelled(this); });
        }

        void offer(const std::string& mimeType) override {
            m_source->sendOffer(mimeType.c_str());
        }

        void destroy() override {
            m_source->sendDestroy();
        }

        SP<TSource> m_source;
    };

    void onDataOffer(wl_proxy* proxy) {
        m_announced.emplace_back(makeShared<COffer>(proxy));
    }

    void onSelection(wl_proxy* proxy) {
        SP<IDataControlOffer> selected;

        if (proxy) {
            const auto IT = std::ranges::find_if(m_announced, [proxy](const SP<COffer>& o) { return rc<wl_proxy*>(o->m_offer->resource()) == proxy; });
            if (IT != m_announced.end()) {
                selected = *IT;
                m_announced.erase(IT);
            }
        }

        // Whatever else was announced (a primary selection, say) is of no use to us.
        for (const auto& offer : m_announced)
            offer->destroy();
        m_announced.clear();

        if (m_onSelection)
            m_onSelection(selected);
    }

    SP<TManager> m_manager;
    SP<TDevice>  m_device;
    // Offers announced whose selection hasn't been decided yet.
    std::vector<SP<COffer>> m_announced;
};

std::unique_ptr<IDataControl> makeDataControl(SP<CCExtDataControlManagerV1> manager, SP<CCWlSeat> seat) {
    if (!manager || !seat)
        return nullptr;

    return std::make_unique<CDataControl<CCExtDataControlManagerV1, CCExtDataControlDeviceV1, CCExtDataControlSourceV1, CCExtDataControlOfferV1>>(manager, seat);
}

std::unique_ptr<IDataControl> makeDataControl(SP<CCZwlrDataControlManagerV1> manager, SP<CCWlSeat> seat) {
    if (!manager || !seat)
        return nullptr;

    return std::make_unique<CDataControl<CCZwlrDataControlManagerV1, CCZwlrDataControlDeviceV1, CCZwlrDataControlSourceV1, CCZwlrDataControlOfferV1>>(manager, seat);
}
