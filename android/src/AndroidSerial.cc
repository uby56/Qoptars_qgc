#include "AndroidSerial.h"
#include "AndroidInterface.h"

#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <QtCore/QCoreApplication>
#include <qserialport_android_p.h>
#include <qserialportinfo_p.h>

#include <jni.h>

Q_LOGGING_CATEGORY(AndroidSerialLog, "qgc.android.serial");

namespace AndroidSerial
{

void setNativeMethods()
{
    qCDebug(AndroidSerialLog) << "Registering Native Functions";

    JNINativeMethod javaMethods[] {
        {"nativeDeviceHasDisconnected", "(J)V",                     reinterpret_cast<void*>(jniDeviceHasDisconnected)},
        {"nativeDeviceNewData",         "(J[B)V",                   reinterpret_cast<void*>(jniDeviceNewData)},
        {"nativeDeviceException",       "(JLjava/lang/String;)V",   reinterpret_cast<void*>(jniDeviceException)},
    };

    (void) AndroidInterface::cleanJavaException();

    jclass objectClass = AndroidInterface::getActivityClass();
    if (!objectClass) {
        qCWarning(AndroidSerialLog) << "Couldn't find class:" << objectClass;
        return;
    }

    QJniEnvironment jniEnv;
    jint val = jniEnv->RegisterNatives(objectClass, javaMethods, sizeof(javaMethods) / sizeof(javaMethods[0]));

    if (val < 0) {
        qCWarning(AndroidSerialLog) << "Error registering methods: " << val;
    } else {
        qCDebug(AndroidSerialLog) << "Native Functions Registered";
    }

    (void) AndroidInterface::cleanJavaException();
}

void jniDeviceHasDisconnected(JNIEnv *env, jobject obj, jlong userData)
{
    Q_UNUSED(env);
    Q_UNUSED(obj);

    if (userData != 0) {
        QSerialPortPrivate* const serialPort = reinterpret_cast<QSerialPortPrivate*>(userData);
        qCDebug(AndroidSerialLog) << "Device disconnected" << serialPort->systemLocation.toLatin1().data();
        serialPort->q_ptr->close();
    }
}

void jniDeviceNewData(JNIEnv *env, jobject obj, jlong userData, jbyteArray data)
{
    Q_UNUSED(obj);

    if (userData != 0) {
        jbyte* const bytes = env->GetByteArrayElements(data, nullptr);
        const jsize len = env->GetArrayLength(data);
        // QByteArray data = QByteArray::fromRawData(reinterpret_cast<char*>(bytes), len);
        QSerialPortPrivate* const serialPort = reinterpret_cast<QSerialPortPrivate*>(userData);
        serialPort->newDataArrived(reinterpret_cast<char*>(bytes), len);
        env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
    }
}

void jniDeviceException(JNIEnv *env, jobject obj, jlong userData, jstring message)
{
    Q_UNUSED(obj);

    if (userData != 0) {
        const char* const string = env->GetStringUTFChars(message, nullptr);
        const QString str = QString::fromUtf8(string);
        env->ReleaseStringUTFChars(message, string);
        (void) QJniEnvironment::checkAndClearExceptions(env);
        (reinterpret_cast<QSerialPortPrivate*>(userData))->exceptionArrived(str);
    }
}

QList<QSerialPortInfo> availableDevices()
{
    jclass javaClass = AndroidInterface::getActivityClass();
    if (!javaClass) {
        qCWarning(AndroidSerialLog) << "Class Not Found";
        return QList<QSerialPortInfo>();
    }

    QJniEnvironment env;
    const jmethodID methodId = env.findStaticMethod(javaClass, "availableDevicesInfo", "()[Ljava/lang/String;");
    if (!methodId) {
        qCWarning(AndroidSerialLog) << "Method Not Found";
        return QList<QSerialPortInfo>();
    }

    const QJniObject result = QJniObject::callStaticObjectMethod(javaClass, methodId);
    if (!result.isValid()) {
        qCDebug(AndroidSerialLog) << "Method Call Failed";
        return QList<QSerialPortInfo>();
    }

    QList<QSerialPortInfo> serialPortInfoList;

    const jobjectArray objArray = result.object<jobjectArray>();
    const jsize count = env->GetArrayLength(objArray);

    for (jsize i = 0; i < count; i++) {
        jobject obj = env->GetObjectArrayElement(objArray, i);
        jstring string = static_cast<jstring>(obj);
        const char * const rawString = env->GetStringUTFChars(string, 0);
        qCDebug(AndroidSerialLog) << "Adding device" << rawString;

        const QStringList strList = QString::fromUtf8(rawString).split(QStringLiteral(":"));
        env->ReleaseStringUTFChars(string, rawString);
        env->DeleteLocalRef(string);

        if (strList.size() < 4) {
            qCWarning(AndroidSerialLog) << "Invalid device info";
            continue;
        }

        QSerialPortInfoPrivate priv;

        priv.portName             = strList.at(0);
        priv.device               = strList.at(0);
        priv.description          = "";
        priv.manufacturer         = strList.at(1);
        priv.serialNumber         = ""; // getSerialNumber(getDeviceId(priv.portName));
        priv.productIdentifier    = strList.at(2).toInt();
        priv.hasProductIdentifier = (priv.productIdentifier != BAD_PORT);
        priv.vendorIdentifier     = strList.at(3).toInt();
        priv.hasVendorIdentifier  = (priv.vendorIdentifier != BAD_PORT);

        (void) serialPortInfoList.append(priv);
    }

    return serialPortInfoList;
}

QString getSerialNumber(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const QJniObject result = QJniObject::callStaticMethod<jobject>(
        AndroidInterface::getActivityClass(),
        "getSerialNumber",
        "(I)Ljava/lang/String;",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    if (!result.isValid()) {
        qCWarning(AndroidSerialLog) << "Invalid Result";
        return QString();
    }

    return result.toString();
}

int getDeviceId(const QString &portName)
{
    QJniObject name = QJniObject::fromString(portName);

    (void) AndroidInterface::cleanJavaException();
    const int result = QJniObject::callStaticMethod<jint>(
        AndroidInterface::getActivityClass(),
        "getDeviceId",
        "(Ljava/lang/String;)I",
        name.object<jstring>()
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

int open(const QString &portName, QSerialPortPrivate *userData)
{
    QJniObject name = QJniObject::fromString(portName);

    (void) AndroidInterface::cleanJavaException();
    const int deviceId = QJniObject::callStaticMethod<jint>(
        AndroidInterface::getActivityClass(),
        "open",
        "(Landroid/content/Context;Ljava/lang/String;J)I",
        QNativeInterface::QAndroidApplication::context(),
        name.object<jstring>(),
        reinterpret_cast<jlong>(userData)
    );
    (void) AndroidInterface::cleanJavaException();

    return deviceId;
}

bool close(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "close",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool isOpen(const QString &portName)
{
    QJniObject name = QJniObject::fromString(portName);

    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "isDeviceNameOpen",
        "(Ljava/lang/String;)Z",
        name.object<jstring>()
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

QByteArray read(int deviceId, int length, int timeout)
{
    (void) AndroidInterface::cleanJavaException();
    const QJniObject result = QJniObject::callStaticMethod<jbyteArray>(
        AndroidInterface::getActivityClass(),
        "read",
        "(II)[B",
        deviceId,
        length,
        timeout
    );
    (void) AndroidInterface::cleanJavaException();

    jbyteArray jarray = result.object<jbyteArray>();
    QJniEnvironment jniEnv;
    jbyte* const bytes = jniEnv->GetByteArrayElements(jarray, nullptr);
    const jsize len = jniEnv->GetArrayLength(jarray);
    QByteArray data = QByteArray::fromRawData(reinterpret_cast<char*>(bytes), len);
    jniEnv->ReleaseByteArrayElements(jarray, bytes, JNI_ABORT);

    return data;
}

int write(int deviceId, QByteArrayView data, int length, int timeout, bool async)
{
    QJniEnvironment jniEnv;
    jbyteArray jarray = jniEnv->NewByteArray(static_cast<jsize>(length));
    jniEnv->SetByteArrayRegion(jarray, 0, static_cast<jsize>(length), (jbyte*)data.constData());

    int result;
    (void) AndroidInterface::cleanJavaException();
    if (async) {
        QJniObject::callStaticMethod<void>(
            AndroidInterface::getActivityClass(),
            "writeAsync",
            "(I[B)V",
            deviceId,
            jarray
        );
        result = 0;
    } else {
        result = QJniObject::callStaticMethod<jint>(
            AndroidInterface::getActivityClass(),
            "write",
            "(I[BI)I",
            deviceId,
            jarray,
            timeout
        );
    }
    (void) AndroidInterface::cleanJavaException();
    jniEnv->DeleteLocalRef(jarray);

    return result;
}

bool setParameters(int deviceId, int baudRate, int dataBits, int stopBits, int parity)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "setParameters",
        "(IIIII)Z",
        deviceId,
        baudRate,
        dataBits,
        stopBits,
        parity
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool getCarrierDetect(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "getCarrierDetect",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool getClearToSend(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "getClearToSend",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool getDataSetReady(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "getDataSetReady",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool getDataTerminalReady(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "getDataTerminalReady",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool setDataTerminalReady(int deviceId, bool set)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "setDataTerminalReady",
        "(IZ)Z",
        deviceId,
        set
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool getRingIndicator(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "getRingIndicator",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool getRequestToSend(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "getRequestToSend",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool setRequestToSend(int deviceId, bool set)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "setRequestToSend",
        "(IZ)Z",
        deviceId,
        set
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

QSerialPort::PinoutSignals getControlLines(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const QJniObject result = QJniObject::callStaticMethod<jintArray>(
        AndroidInterface::getActivityClass(),
        "getControlLines",
        "(I)[I",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    jintArray jarray = result.object<jintArray>();
    QJniEnvironment jniEnv;
    jint* const ints = jniEnv->GetIntArrayElements(jarray, nullptr);
    const jsize len = jniEnv->GetArrayLength(jarray);

    QSerialPort::PinoutSignals data = QSerialPort::PinoutSignals::fromInt(0);
    for (jsize i = 0; i < len; i++) {
        const jint value = ints[i];
        if ((value <= ControlLine::UnknownControlLine) || (value >= ControlLine::RiControlLine)) {
            continue;
        }

        const ControlLine line = static_cast<ControlLine>(value);
        switch (line) {
        case ControlLine::RtsControlLine:
            (void) data.setFlag(QSerialPort::PinoutSignal::RequestToSendSignal, true);
            break;
        case ControlLine::CtsControlLine:
            (void) data.setFlag(QSerialPort::PinoutSignal::ClearToSendSignal, true);
            break;
        case ControlLine::DtrControlLine:
            (void) data.setFlag(QSerialPort::PinoutSignal::DataTerminalReadySignal, true);
            break;
        case ControlLine::DsrControlLine:
            (void) data.setFlag(QSerialPort::PinoutSignal::DataSetReadySignal, true);
            break;
        case ControlLine::CdControlLine:
            (void) data.setFlag(QSerialPort::PinoutSignal::DataCarrierDetectSignal, true);
            break;
        case ControlLine::RiControlLine:
            (void) data.setFlag(QSerialPort::PinoutSignal::RingIndicatorSignal, true);
            break;
        case ControlLine::UnknownControlLine:
        default:
            break;
        }
    }
    jniEnv->ReleaseIntArrayElements(jarray, ints, JNI_ABORT);

    return data;
}

int getFlowControl(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const int result = QJniObject::callStaticMethod<jint>(
        AndroidInterface::getActivityClass(),
        "getFlowControl",
        "(I)I",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return static_cast<QSerialPort::FlowControl>(result);
}

bool setFlowControl(int deviceId, int flowControl)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "setFlowControl",
        "(II)Z",
        deviceId,
        flowControl
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool flush(int deviceId, bool input, bool output)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "purgeBuffers",
        "(IZZ)Z",
        deviceId,
        input,
        output
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool setBreak(int deviceId, bool set)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "setBreak",
        "(IZ)Z",
        deviceId,
        set
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool startReadThread(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "startIoManager",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

bool stopReadThread(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const bool result = QJniObject::callStaticMethod<jboolean>(
        AndroidInterface::getActivityClass(),
        "stopIoManager",
        "(I)Z",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

int getDeviceHandle(int deviceId)
{
    (void) AndroidInterface::cleanJavaException();
    const int result = QJniObject::callStaticMethod<jint>(
        AndroidInterface::getActivityClass(),
        "getDeviceHandle",
        "(I)I",
        deviceId
    );
    (void) AndroidInterface::cleanJavaException();

    return result;
}

} // namespace AndroidSerial
