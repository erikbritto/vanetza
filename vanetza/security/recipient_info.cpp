#include <vanetza/security/recipient_info.hpp>
#include <vanetza/security/length_coding.hpp>

namespace vanetza
{
namespace security
{

PublicKeyAlgorithm get_type(const RecipientInfo& info)
{
    struct RecipientInfoKey_visitor : public boost::static_visitor<PublicKeyAlgorithm>
    {
        PublicKeyAlgorithm operator()(const EciesNistP256EncryptedKey& key)
        {
            return PublicKeyAlgorithm::Ecies_Nistp256;
        }

        PublicKeyAlgorithm operator()(const ByteBuffer& key)
        {
            // TODO: could be anything except Ecies_Nistp256
            return PublicKeyAlgorithm::Ecdsa_Nistp256_With_Sha256;
        }
    };

    RecipientInfoKey_visitor visit;
    return boost::apply_visitor(visit, info.enc_key);
}

PublicKeyAlgorithm RecipientInfo::pk_encryption() const
{
    return get_type(*this);
}

size_t get_size(const RecipientInfo& info)
{
    size_t size = info.cert_id.size();
    size += sizeof(PublicKeyAlgorithm);
    struct RecipientInfoKey_visitor : public boost::static_visitor<size_t>
    {
        size_t operator()(const EciesNistP256EncryptedKey& key)
        {
            return key.c.size() + key.t.size() + get_size(key.v);
        }

        size_t operator()(const ByteBuffer& key)
        {
            return length_coding_size(key.size()) + key.size();
        }
    };

    RecipientInfoKey_visitor visit;
    size += boost::apply_visitor(visit, info.enc_key);
    return size;
}

void serialize(OutputArchive& ar, const RecipientInfo& info, SymmetricAlgorithm sym_algo)
{
    struct key_visitor : public boost::static_visitor<>
    {
        key_visitor(OutputArchive& ar, SymmetricAlgorithm sym_algo, PublicKeyAlgorithm pk_algo) :
            m_archive(ar), m_sym_algo(sym_algo), m_pk_algo(pk_algo)
        {
        }
        void operator()(const EciesNistP256EncryptedKey& key)
        {
            assert(key.c.size() == field_size(m_sym_algo));
            serialize(m_archive, key.v, m_pk_algo);
            for (auto& byte : key.c) {
                m_archive << byte;
            }
            for (auto& byte : key.t) {
                m_archive << byte;
            }
        }

        void operator()(const ByteBuffer& key)
        {
            serialize_length(m_archive, key.size());
            for (auto byte : key) {
                m_archive << byte;
            }
        }

        OutputArchive& m_archive;
        SymmetricAlgorithm m_sym_algo;
        PublicKeyAlgorithm m_pk_algo;
    };

    for (auto& byte : info.cert_id) {
        ar << byte;
    }
    const PublicKeyAlgorithm pk_algo = info.pk_encryption();
    serialize(ar, pk_algo);
    key_visitor visitor(ar, sym_algo, pk_algo);
    boost::apply_visitor(visitor, info.enc_key);
}

size_t deserialize(InputArchive& ar, RecipientInfo& info, const SymmetricAlgorithm& symAlgo)
{
    for (size_t c = 0; c < info.cert_id.size(); ++c) {
        ar >> info.cert_id[c];
    }
    PublicKeyAlgorithm algo;
    deserialize(ar, algo);
    switch (algo) {
        case PublicKeyAlgorithm::Ecies_Nistp256: {
            EciesNistP256EncryptedKey ecies;
            deserialize(ar, ecies.v, PublicKeyAlgorithm::Ecies_Nistp256);
            const size_t fieldSize = field_size(symAlgo);
            for (size_t c = 0; c < fieldSize; ++c) {
                uint8_t tmp;
                ar >> tmp;
                ecies.c.push_back(tmp);
            }
            for (size_t c = 0; c < ecies.t.size(); ++c) {
                uint8_t tmp;
                ar >> tmp;
                ecies.t[c] = tmp;
            }
            info.enc_key = std::move(ecies);
            break;
        }
        default: {
            const size_t length = deserialize_length(ar);
            ByteBuffer opaque(length);
            for (size_t i = 0; i < length; ++i) {
                ar >> opaque[i];
            }
            info.enc_key = std::move(opaque);
            break;
        }
    }
    return get_size(info);
}

} // namespace security
} // namespace vanetza
