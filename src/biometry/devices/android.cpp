/*
 * Copyright (C) 2020 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Erfan Abdi <erfangplus@gmail.com>
 *2
 */

#include <biometry/devices/android.h>
#include <biometry/util/configuration.h>
#include <biometry/util/not_implemented.h>

#include <biometry/device_registry.h>

std::string IntToStringFingerprintError(int error, int vendorCode){
    switch(error) {
        case ERROR_NO_ERROR: return "ERROR_NO_ERROR";
        case ERROR_HW_UNAVAILABLE: return "ERROR_HW_UNAVAILABLE";
        case ERROR_UNABLE_TO_PROCESS: return "ERROR_UNABLE_TO_PROCESS";
        case ERROR_TIMEOUT: return "ERROR_TIMEOUT";
        case ERROR_NO_SPACE: return "ERROR_NO_SPACE";
        case ERROR_CANCELED: return "ERROR_CANCELED";
        case ERROR_UNABLE_TO_REMOVE: return "ERROR_UNABLE_TO_REMOVE";
        case ERROR_LOCKOUT: return "ERROR_LOCKOUT";
        case ERROR_VENDOR: return "ERROR_VENDOR: " + std::to_string(vendorCode);
        default:
            return "ERROR_NO_ERROR";
    }
}

namespace
{
class androidEnrollOperation : public biometry::Operation<biometry::TemplateStore::Enrollment>
{
public:
    typename biometry::Operation<biometry::TemplateStore::Enrollment>::Observer::Ptr mobserver;
    UHardwareBiometry hybris_fp_instance;
    int totalrem = 0;
    
    androidEnrollOperation(UHardwareBiometry hybris_fp_instance)
     : hybris_fp_instance{hybris_fp_instance}
    {
    }

    void start_with_observer(const typename biometry::Operation<biometry::TemplateStore::Enrollment>::Observer::Ptr& observer) override
    {
        mobserver = observer;
        observer->on_started();
        UHardwareBiometryParams fp_params;

        fp_params.enrollresult_cb = enrollresult_cb;
        fp_params.acquired_cb = acquired_cb;
        fp_params.authenticated_cb = authenticated_cb;
        fp_params.error_cb = error_cb;
        fp_params.removed_cb = removed_cb;
        fp_params.enumerate_cb = enumerate_cb;
        fp_params.context = this;

        u_hardware_biometry_setNotify(hybris_fp_instance, &fp_params);
    }

    void cancel() override
    {
        u_hardware_biometry_cancel(hybris_fp_instance);
    }

private:
    static void acquired_cb(uint64_t, UHardwareBiometryFingerprintAcquiredInfo, int32_t, void *){}
    static void authenticated_cb(uint64_t, uint32_t, uint32_t, void *){}
    static void removed_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}
    static void enumerate_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}

    static void enrollresult_cb(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining, void *context)
    {
        if (remaining > 0)
        {
            if (((androidEnrollOperation*)context)->totalrem == 0)
                ((androidEnrollOperation*)context)->totalrem = remaining + 1;
            float raw_value = 1 - (remaining / ((androidEnrollOperation*)context)->totalrem);
            ((androidEnrollOperation*)context)->mobserver->on_progress(biometry::Progress{biometry::Percent::from_raw_value(raw_value), biometry::Dictionary{}});
        } else {
            ((androidEnrollOperation*)context)->mobserver->on_progress(biometry::Progress{biometry::Percent::from_raw_value(1), biometry::Dictionary{}});
            UHardwareBiometryRequestStatus ret = u_hardware_biometry_postEnroll(((androidEnrollOperation*)context)->hybris_fp_instance);
            ((androidEnrollOperation*)context)->mobserver->on_succeeded(fingerId);
        }
    }
    
    static void error_cb(uint64_t, UHardwareBiometryFingerprintError error, int32_t vendorCode, void *context)
    {
        if (error == 0)
            return;

        ((androidEnrollOperation*)context)->mobserver->on_failed(IntToStringFingerprintError(error, vendorCode));
    }
    
};
class androidRemovalOperation : public biometry::Operation<biometry::TemplateStore::Removal>
{
public:
    typename biometry::Operation<biometry::TemplateStore::Removal>::Observer::Ptr mobserver;

    androidRemovalOperation(UHardwareBiometry hybris_fp_instance, uint32_t finger)
     : hybris_fp_instance{hybris_fp_instance},
       finger{finger}
    {
    }

    void start_with_observer(const typename biometry::Operation<biometry::TemplateStore::Removal>::Observer::Ptr& observer) override
    {
        mobserver = observer;
        observer->on_started();
        UHardwareBiometryParams fp_params;

        fp_params.enrollresult_cb = enrollresult_cb;
        fp_params.acquired_cb = acquired_cb;
        fp_params.authenticated_cb = authenticated_cb;
        fp_params.error_cb = error_cb;
        fp_params.removed_cb = removed_cb;
        fp_params.enumerate_cb = enumerate_cb;
        fp_params.context = this;

        u_hardware_biometry_setNotify(hybris_fp_instance, &fp_params);
    }

    void cancel() override
    {
        u_hardware_biometry_cancel(hybris_fp_instance);
    }

private:
    UHardwareBiometry hybris_fp_instance;
    uint32_t finger;

    static void enrollresult_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}
    static void acquired_cb(uint64_t, UHardwareBiometryFingerprintAcquiredInfo, int32_t, void *){}
    static void authenticated_cb(uint64_t, uint32_t, uint32_t, void *){}
    static void enumerate_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}

    static void removed_cb(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining, void *context)
    {
        if (fingerId == ((androidRemovalOperation*)context)->finger && remaining == 0)
            ((androidRemovalOperation*)context)->mobserver->on_succeeded(fingerId);
    }
    static void error_cb(uint64_t, UHardwareBiometryFingerprintError error, int32_t vendorCode, void *context)
    {
        if (error == 0)
            return;
        
        ((androidRemovalOperation*)context)->mobserver->on_failed(IntToStringFingerprintError(error, vendorCode));
    }
};
template<typename T>
class androidAuthenticateOperation : public biometry::Operation<T>
{
public:
    typename biometry::Operation<T>::Observer::Ptr mobserver;
    
    androidAuthenticateOperation(UHardwareBiometry hybris_fp_instance)
     : hybris_fp_instance{hybris_fp_instance}
    {
    }
    
    void start_with_observer(const typename biometry::Operation<T>::Observer::Ptr& observer) override
    {
        mobserver = observer;
        observer->on_started();
        UHardwareBiometryParams fp_params;
        
        fp_params.enrollresult_cb = enrollresult_cb;
        fp_params.acquired_cb = acquired_cb;
        fp_params.authenticated_cb = authenticated_cb;
        fp_params.error_cb = error_cb;
        fp_params.removed_cb = removed_cb;
        fp_params.enumerate_cb = enumerate_cb;
        fp_params.context = this;
        
        u_hardware_biometry_setNotify(hybris_fp_instance, &fp_params);
    }
    
    void cancel() override
    {
        u_hardware_biometry_cancel(hybris_fp_instance);
    }
    
private:
    UHardwareBiometry hybris_fp_instance;
    
    static void enrollresult_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}
    static void acquired_cb(uint64_t, UHardwareBiometryFingerprintAcquiredInfo, int32_t, void *){}
    static void removed_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}
    static void enumerate_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}

    static void authenticated_cb(uint64_t, uint32_t fingerId, uint32_t, void *context)
    {
        ((androidRemovalOperation*)context)->mobserver->on_succeeded(fingerId);
    }
    static void error_cb(uint64_t, UHardwareBiometryFingerprintError error, int32_t vendorCode, void *context)
    {
        if (error == 0)
            return;
        
        ((androidAuthenticateOperation*)context)->mobserver->on_failed(IntToStringFingerprintError(error, vendorCode));
    }
};
class androidListOperation : public biometry::Operation<biometry::TemplateStore::List>
{
public:
    typename biometry::Operation<biometry::TemplateStore::List>::Observer::Ptr mobserver;
    int totalrem = 0;
    std::vector<uint64_t> result;
    
    androidListOperation(UHardwareBiometry hybris_fp_instance)
     : hybris_fp_instance{hybris_fp_instance}
    {
    }
    
    void start_with_observer(const typename biometry::Operation<biometry::TemplateStore::List>::Observer::Ptr& observer) override
    {
        mobserver = observer;
        observer->on_started();
        UHardwareBiometryParams fp_params;
        
        fp_params.enrollresult_cb = enrollresult_cb;
        fp_params.acquired_cb = acquired_cb;
        fp_params.authenticated_cb = authenticated_cb;
        fp_params.error_cb = error_cb;
        fp_params.removed_cb = removed_cb;
        fp_params.enumerate_cb = enumerate_cb;
        fp_params.context = this;
        
        u_hardware_biometry_setNotify(hybris_fp_instance, &fp_params);
    }
    
    void cancel() override
    {
        u_hardware_biometry_cancel(hybris_fp_instance);
    }
    
private:
    UHardwareBiometry hybris_fp_instance;

    static void enrollresult_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}
    static void acquired_cb(uint64_t, UHardwareBiometryFingerprintAcquiredInfo, int32_t, void *){}
    static void authenticated_cb(uint64_t, uint32_t, uint32_t, void *){}
    static void removed_cb(uint64_t, uint32_t, uint32_t, uint32_t, void *){}
    static void enumerate_cb(uint64_t, uint32_t fingerId, uint32_t, uint32_t remaining, void *context)
    {
        if (remaining > 0)
        {
            if (((androidListOperation*)context)->totalrem == 0) {
                ((androidListOperation*)context)->totalrem = remaining + 1;
                ((androidListOperation*)context)->result.clear();
            }
            float raw_value = 1 - (remaining / ((androidListOperation*)context)->totalrem);
            ((androidListOperation*)context)->mobserver->on_progress(biometry::Progress{biometry::Percent::from_raw_value(raw_value), biometry::Dictionary{}});
            ((androidListOperation*)context)->result.push_back(fingerId);
        } else {
            ((androidListOperation*)context)->mobserver->on_progress(biometry::Progress{biometry::Percent::from_raw_value(1), biometry::Dictionary{}});
            ((androidListOperation*)context)->mobserver->on_succeeded(((androidListOperation*)context)->result);
        }
    }
    
    static void error_cb(uint64_t, UHardwareBiometryFingerprintError error, int32_t vendorCode, void *context)
    {
        if (error == 0)
            return;
        
        ((androidListOperation*)context)->mobserver->on_failed(IntToStringFingerprintError(error, vendorCode));
    }
    
};
}

biometry::devices::android::TemplateStore::TemplateStore(UHardwareBiometry hybris_fp_instance)
    : hybris_fp_instance{hybris_fp_instance}
{
}

biometry::Operation<biometry::TemplateStore::SizeQuery>::Ptr biometry::devices::android::TemplateStore::size(const biometry::Application&, const biometry::User&)
{
    //TODO: enumerate and count remaining int
    return std::make_shared<androidOperation<biometry::TemplateStore::SizeQuery>>(hybris_fp_instance);
}

biometry::Operation<biometry::TemplateStore::List>::Ptr biometry::devices::android::TemplateStore::list(const biometry::Application&, const biometry::User&)
{
    UHardwareBiometryRequestStatus ret = u_hardware_biometry_enumerate(hybris_fp_instance);
    return std::make_shared<androidListOperation>(hybris_fp_instance);
}

biometry::Operation<biometry::TemplateStore::Enrollment>::Ptr biometry::devices::android::TemplateStore::enroll(const biometry::Application&, const biometry::User&)
{
    uint64_t token = u_hardware_biometry_preEnroll(hybris_fp_instance);
    UHardwareBiometryRequestStatus ret = u_hardware_biometry_enroll(hybris_fp_instance, new uint8_t[69], 0, 1000);
    return std::make_shared<androidEnrollOperation>(hybris_fp_instance);
}

biometry::Operation<biometry::TemplateStore::Removal>::Ptr biometry::devices::android::TemplateStore::remove(const biometry::Application&, const biometry::User& user, biometry::TemplateStore::TemplateId id)
{
    UHardwareBiometryRequestStatus ret = u_hardware_biometry_remove(hybris_fp_instance, 0, id);
    return std::make_shared<androidRemovalOperation>(hybris_fp_instance, id);
}

biometry::Operation<biometry::TemplateStore::Clearance>::Ptr biometry::devices::android::TemplateStore::clear(const biometry::Application&, const biometry::User&)
{
    //TODO: enumerate + remove
    return std::make_shared<androidOperation<biometry::TemplateStore::Clearance>>(hybris_fp_instance);
}

biometry::devices::android::Identifier::Identifier(UHardwareBiometry hybris_fp_instance)
    : hybris_fp_instance{hybris_fp_instance}
{
}

biometry::Operation<biometry::Identification>::Ptr biometry::devices::android::Identifier::identify_user(const biometry::Application&, const biometry::Reason&)
{
    UHardwareBiometryRequestStatus ret = u_hardware_biometry_authenticate(hybris_fp_instance, 0, 0);
    return std::make_shared<androidAuthenticateOperation<biometry::Identification>>(hybris_fp_instance);
}

biometry::devices::android::Verifier::Verifier(UHardwareBiometry hybris_fp_instance)
    : hybris_fp_instance{hybris_fp_instance}
{
}

biometry::Operation<biometry::Verification>::Ptr biometry::devices::android::Verifier::verify_user(const biometry::Application&, const biometry::User&, const biometry::Reason&)
{
    UHardwareBiometryRequestStatus ret = u_hardware_biometry_authenticate(hybris_fp_instance, 0, 0);
    return std::make_shared<androidAuthenticateOperation<biometry::Verification>>(hybris_fp_instance);
}

biometry::devices::android::android(UHardwareBiometry hybris_fp_instance)
    : template_store_{hybris_fp_instance},
      identifier_{hybris_fp_instance},
      verifier_{hybris_fp_instance}
{
    //setActiveGroup
}

biometry::TemplateStore& biometry::devices::android::template_store()
{
    return template_store_;
}

biometry::Identifier& biometry::devices::android::identifier()
{
    return identifier_;
}

biometry::Verifier& biometry::devices::android::verifier()
{
    return verifier_;
}

namespace
{
struct androidDescriptor : public biometry::Device::Descriptor
{
    std::shared_ptr<biometry::Device> create(const biometry::util::Configuration&) override
    {
        return std::make_shared<biometry::devices::android>(u_hardware_biometry_new());
    }

    std::string name() const override
    {
        return "Android HAL Bridge";
    }

    std::string author() const override
    {
        return "Erfan Abdi (erfangplus@gmail.com)";
    }

    std::string description() const override
    {
        return "android is a biometry::Device implementation for connecting to android fp hal using hybris.";
    }
};
}

biometry::Device::Descriptor::Ptr biometry::devices::android::make_descriptor()
{
    return std::make_shared<androidDescriptor>();
}
