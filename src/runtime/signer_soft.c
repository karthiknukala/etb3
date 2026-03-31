#include "etb/signer.h"

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct etb_apple_signer {
  SecKeyRef private_key;
  SecKeyRef public_key;
} etb_apple_signer;

static char *etb_hex_encode(const unsigned char *data, size_t size) {
  static const char LUT[] = "0123456789abcdef";
  char *hex = (char *)malloc(size * 2U + 1U);
  size_t index;
  if (hex == NULL) {
    return NULL;
  }
  for (index = 0U; index < size; ++index) {
    hex[index * 2U] = LUT[data[index] >> 4U];
    hex[index * 2U + 1U] = LUT[data[index] & 0x0fU];
  }
  hex[size * 2U] = '\0';
  return hex;
}

static unsigned char etb_hex_nibble(char ch) {
  if (ch >= '0' && ch <= '9') return (unsigned char)(ch - '0');
  if (ch >= 'a' && ch <= 'f') return (unsigned char)(10 + ch - 'a');
  if (ch >= 'A' && ch <= 'F') return (unsigned char)(10 + ch - 'A');
  return 0U;
}

static unsigned char *etb_hex_decode(const char *hex, size_t *size) {
  unsigned char *data;
  size_t index;
  size_t length = strlen(hex);
  if (length % 2U != 0U) {
    return NULL;
  }
  *size = length / 2U;
  data = (unsigned char *)malloc(*size);
  if (data == NULL) {
    return NULL;
  }
  for (index = 0U; index < *size; ++index) {
    data[index] = (unsigned char)((etb_hex_nibble(hex[index * 2U]) << 4U) |
                                  etb_hex_nibble(hex[index * 2U + 1U]));
  }
  return data;
}

static bool etb_create_p256_key(bool secure_enclave, etb_signer *signer,
                                const char *label, char *error,
                                size_t error_size) {
  CFMutableDictionaryRef attrs;
  CFMutableDictionaryRef private_attrs;
  CFErrorRef cf_error = NULL;
  etb_apple_signer *impl;

  impl = (etb_apple_signer *)calloc(1U, sizeof(*impl));
  if (impl == NULL) {
    snprintf(error, error_size, "out of memory");
    return false;
  }

  attrs = CFDictionaryCreateMutable(NULL, 0U, &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
  private_attrs = CFDictionaryCreateMutable(NULL, 0U,
                                            &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
  if (attrs == NULL || private_attrs == NULL) {
    snprintf(error, error_size, "failed to allocate key attributes");
    free(impl);
    return false;
  }
  CFDictionarySetValue(attrs, kSecAttrKeyType, kSecAttrKeyTypeECSECPrimeRandom);
  CFDictionarySetValue(attrs, kSecAttrKeySizeInBits, CFSTR("256"));
  CFDictionarySetValue(attrs, kSecPrivateKeyAttrs, private_attrs);
  CFDictionarySetValue(private_attrs, kSecAttrIsPermanent, kCFBooleanFalse);
  if (label != NULL) {
    CFStringRef tag = CFStringCreateWithCString(NULL, label, kCFStringEncodingUTF8);
    CFDictionarySetValue(private_attrs, kSecAttrLabel, tag);
    CFRelease(tag);
  }
  if (secure_enclave) {
    CFDictionarySetValue(attrs, kSecAttrTokenID, kSecAttrTokenIDSecureEnclave);
  }
  impl->private_key = SecKeyCreateRandomKey(attrs, &cf_error);
  CFRelease(private_attrs);
  CFRelease(attrs);
  if (impl->private_key == NULL) {
    snprintf(error, error_size, "failed to create P-256 key");
    free(impl);
    if (cf_error != NULL) {
      CFRelease(cf_error);
    }
    return false;
  }
  impl->public_key = SecKeyCopyPublicKey(impl->private_key);
  signer->kind = secure_enclave ? ETB_SIGNER_MACOS_SECURE_ENCLAVE : ETB_SIGNER_SOFT;
  signer->impl = impl;
  return true;
}

bool etb_signer_open_soft(etb_signer *signer, const char *key_path,
                          char *error, size_t error_size) {
  return etb_create_p256_key(false, signer, key_path, error, error_size);
}

bool etb_signer_open_macos(etb_signer *signer, const char *label, char *error,
                           size_t error_size) {
  return etb_create_p256_key(true, signer, label, error, error_size);
}

void etb_signer_close(etb_signer *signer) {
  etb_apple_signer *impl;
  if (signer == NULL || signer->impl == NULL) {
    return;
  }
  impl = (etb_apple_signer *)signer->impl;
  if (impl->private_key != NULL) CFRelease(impl->private_key);
  if (impl->public_key != NULL) CFRelease(impl->public_key);
  free(impl);
  signer->impl = NULL;
}

bool etb_signer_sign(etb_signer *signer, const unsigned char *data,
                     size_t data_size, unsigned char **signature,
                     size_t *signature_size, char **public_key_pem,
                     char *error, size_t error_size) {
  etb_apple_signer *impl = (etb_apple_signer *)signer->impl;
  CFDataRef payload = CFDataCreate(NULL, data, (CFIndex)data_size);
  CFErrorRef cf_error = NULL;
  CFDataRef sig_data;
  CFDataRef pub_data;
  if (payload == NULL) {
    snprintf(error, error_size, "failed to build payload");
    return false;
  }
  sig_data = SecKeyCreateSignature(impl->private_key,
                                   kSecKeyAlgorithmECDSASignatureMessageX962SHA256,
                                   payload, &cf_error);
  CFRelease(payload);
  if (sig_data == NULL) {
    snprintf(error, error_size, "failed to sign payload");
    if (cf_error != NULL) CFRelease(cf_error);
    return false;
  }
  *signature_size = (size_t)CFDataGetLength(sig_data);
  *signature = (unsigned char *)malloc(*signature_size);
  if (*signature == NULL) {
    CFRelease(sig_data);
    snprintf(error, error_size, "out of memory");
    return false;
  }
  memcpy(*signature, CFDataGetBytePtr(sig_data), *signature_size);
  CFRelease(sig_data);
  pub_data = SecKeyCopyExternalRepresentation(impl->public_key, &cf_error);
  if (pub_data == NULL) {
    free(*signature);
    snprintf(error, error_size, "failed to export public key");
    if (cf_error != NULL) CFRelease(cf_error);
    return false;
  }
  *public_key_pem = etb_hex_encode(CFDataGetBytePtr(pub_data),
                                   (size_t)CFDataGetLength(pub_data));
  CFRelease(pub_data);
  return *public_key_pem != NULL;
}

bool etb_signer_verify(const unsigned char *data, size_t data_size,
                       const unsigned char *signature, size_t signature_size,
                       const char *public_key_pem, char *error,
                       size_t error_size) {
  size_t pub_size;
  unsigned char *pub_bytes = etb_hex_decode(public_key_pem, &pub_size);
  CFDataRef key_data;
  CFMutableDictionaryRef attrs;
  SecKeyRef key;
  CFDataRef payload;
  CFDataRef sig_data;
  CFErrorRef cf_error = NULL;
  bool ok;
  if (pub_bytes == NULL) {
    snprintf(error, error_size, "invalid public key encoding");
    return false;
  }
  key_data = CFDataCreate(NULL, pub_bytes, (CFIndex)pub_size);
  free(pub_bytes);
  attrs = CFDictionaryCreateMutable(NULL, 0U, &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(attrs, kSecAttrKeyType, kSecAttrKeyTypeECSECPrimeRandom);
  CFDictionarySetValue(attrs, kSecAttrKeyClass, kSecAttrKeyClassPublic);
  CFDictionarySetValue(attrs, kSecAttrKeySizeInBits, CFSTR("256"));
  key = SecKeyCreateWithData(key_data, attrs, &cf_error);
  CFRelease(attrs);
  CFRelease(key_data);
  if (key == NULL) {
    snprintf(error, error_size, "failed to import public key");
    if (cf_error != NULL) CFRelease(cf_error);
    return false;
  }
  payload = CFDataCreate(NULL, data, (CFIndex)data_size);
  sig_data = CFDataCreate(NULL, signature, (CFIndex)signature_size);
  ok = SecKeyVerifySignature(key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256,
                             payload, sig_data, &cf_error);
  CFRelease(payload);
  CFRelease(sig_data);
  CFRelease(key);
  if (!ok) {
    snprintf(error, error_size, "signature verification failed");
  }
  return ok;
}

#else

bool etb_signer_open_soft(etb_signer *signer, const char *key_path,
                          char *error, size_t error_size) {
  (void)signer; (void)key_path;
  snprintf(error, error_size, "soft signer is only implemented on Apple platforms");
  return false;
}

bool etb_signer_open_macos(etb_signer *signer, const char *label, char *error,
                           size_t error_size) {
  (void)signer; (void)label;
  snprintf(error, error_size, "Secure Enclave signer is only implemented on Apple platforms");
  return false;
}

void etb_signer_close(etb_signer *signer) { (void)signer; }

bool etb_signer_sign(etb_signer *signer, const unsigned char *data,
                     size_t data_size, unsigned char **signature,
                     size_t *signature_size, char **public_key_pem,
                     char *error, size_t error_size) {
  (void)signer; (void)data; (void)data_size; (void)signature; (void)signature_size;
  (void)public_key_pem;
  snprintf(error, error_size, "signing unavailable on this platform");
  return false;
}

bool etb_signer_verify(const unsigned char *data, size_t data_size,
                       const unsigned char *signature, size_t signature_size,
                       const char *public_key_pem, char *error,
                       size_t error_size) {
  (void)data; (void)data_size; (void)signature; (void)signature_size; (void)public_key_pem;
  snprintf(error, error_size, "verification unavailable on this platform");
  return false;
}

#endif
