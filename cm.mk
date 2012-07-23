# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/gsm.mk)

PRODUCT_RELEASE_NAME := NS
# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_phone.mk)

# Inherit device configuration
$(call inherit-product, device/samsung/crespo/full_crespo.mk)

# Enhanced NFC
$(call inherit-product, vendor/cm/config/nfc_enhanced.mk)

PRODUCT_NAME := cm_crespo
PRODUCT_DEVICE := crespo
PRODUCT_BRAND := Google
PRODUCT_MODEL := Nexus S
PRODUCT_MANUFACTURER := Samsung

#Set build fingerprint / ID / Product Name ect.
PRODUCT_BUILD_PROP_OVERRIDES += PRODUCT_NAME=takju BUILD_FINGERPRINT="google/yakju/crespo:4.1.1/JRO03C/113740:user/release-keys" PRIVATE_BUILD_DESC="takju-user 4.1.1 JRO03C 113740 release-keys"
