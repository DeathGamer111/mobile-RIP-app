#include <QtTest>

#include "NocaiArmStaleSessionRecovery.h"

#if defined(Q_OS_LINUX) && defined(__aarch64__)
#include <fcntl.h>
#include <semaphore.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#endif

class NocaiArmStaleSessionRecoveryTest : public QObject
{
    Q_OBJECT

private slots:
    void clearsCrashStaleSelectedPrinterLock();
    void preservesAvailableSelectedPrinterLock();
};

#if defined(Q_OS_LINUX) && defined(__aarch64__)
namespace {

constexpr std::size_t kRecordOffset = 8;
constexpr std::size_t kPcIpOffset = kRecordOffset + 276;
constexpr std::size_t kPrinterMacOffset = kRecordOffset + 280;

struct Fixture
{
    std::array<unsigned char, 8 + 312> addressList{};
    std::string semaphoreName;

    Fixture()
    {
        const std::uint32_t count = 1;
        const std::uint32_t pcIp = 0x7f00feca;
        const std::array<unsigned char, 6> printerMac{
            0x02, 0x91, 0x82, 0x73, 0x64, 0x55
        };
        std::memcpy(addressList.data(), &count, sizeof(count));
        std::memcpy(addressList.data() + kPcIpOffset, &pcIp, sizeof(pcIp));
        std::memcpy(addressList.data() + kPrinterMacOffset,
                    printerMac.data(), printerMac.size());

        std::uint64_t macValue = 0;
        for (int index = 5; index >= 0; --index) {
            macValue <<= 8;
            macValue |= printerMac[static_cast<std::size_t>(index)];
        }
        semaphoreName = "/SY" + std::to_string(pcIp) +
                        std::to_string(macValue) + "9";
        ::sem_unlink(semaphoreName.c_str());
    }

    ~Fixture()
    {
        ::sem_unlink(semaphoreName.c_str());
    }
};

sem_t* createSemaphore(const std::string& name, unsigned int value)
{
    return ::sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, value);
}

} // namespace
#endif

void NocaiArmStaleSessionRecoveryTest::clearsCrashStaleSelectedPrinterLock()
{
#if defined(Q_OS_LINUX) && defined(__aarch64__)
    Fixture fixture;
    sem_t* semaphore = createSemaphore(fixture.semaphoreName, 1);
    QVERIFY(semaphore != SEM_FAILED);
    QCOMPARE(::sem_wait(semaphore), 0);
    QCOMPARE(::sem_close(semaphore), 0);

    const auto result = NocaiArmStaleSessionRecovery::clearStaleLocalLocks(
        fixture.addressList.data(), 0);
    QVERIFY2(result.completed, result.detail.c_str());
    QCOMPARE(result.inspected, 1);
    QCOMPARE(result.cleared, 1);

    semaphore = ::sem_open(fixture.semaphoreName.c_str(), 0);
    QCOMPARE(semaphore, SEM_FAILED);
#else
    QSKIP("ARM64 Linux-only recovery path");
#endif
}

void NocaiArmStaleSessionRecoveryTest::preservesAvailableSelectedPrinterLock()
{
#if defined(Q_OS_LINUX) && defined(__aarch64__)
    Fixture fixture;
    sem_t* semaphore = createSemaphore(fixture.semaphoreName, 1);
    QVERIFY(semaphore != SEM_FAILED);
    QCOMPARE(::sem_close(semaphore), 0);

    const auto result = NocaiArmStaleSessionRecovery::clearStaleLocalLocks(
        fixture.addressList.data(), 0);
    QVERIFY2(result.completed, result.detail.c_str());
    QCOMPARE(result.inspected, 1);
    QCOMPARE(result.cleared, 0);

    semaphore = ::sem_open(fixture.semaphoreName.c_str(), 0);
    QVERIFY(semaphore != SEM_FAILED);
    QCOMPARE(::sem_close(semaphore), 0);
#else
    QSKIP("ARM64 Linux-only recovery path");
#endif
}

QTEST_MAIN(NocaiArmStaleSessionRecoveryTest)
#include "NocaiArmStaleSessionRecoveryTest.moc"
