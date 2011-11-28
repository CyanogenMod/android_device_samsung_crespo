$(call inherit-product, device/samsung/crespo/full_crespo.mk)

PRODUCT_RELEASE_NAME := NS
# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_phone.mk)

# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/gsm.mk)

PRODUCT_BUILD_PROP_OVERRIDES += PRODUCT_NAME=soju BUILD_ID=GWK74 BUILD_FINGERPRINT=google/soju/crespo:2.3.7/GWK74/185293:user/release-keys PRIVATE_BUILD_DESC="soju-user 2.3.7 GWK74 185293 release-keys" BUILD_NUMBER=185293

PRODUCT_NAME := cm_crespo
PRODUCT_DEVICE := crespo

