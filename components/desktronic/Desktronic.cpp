#include "Desktronic.h"
#include "esphome.h"
#include "esphome/core/log.h"
#include "esphome/components/number/number.h"

namespace esphome
{
namespace desktronic
{

static const char* TAG = "desktronic";

static const uint8_t REMOTE_UART_MESSAGE_LENGTH = 5U;
static const uint8_t REMOTE_UART_MESSAGE_START = 0xa5;
static const uint8_t* REMOTE_UART_MESSAGE_MOVE_UP = new uint8_t[5]{0xa5, 0x00, 0x20, 0xdf, 0xff};
static const uint8_t* REMOTE_UART_MESSAGE_MOVE_DOWN = new uint8_t[5]{0xa5, 0x00, 0x40, 0xbf, 0xff};

static const uint8_t* REMOTE_UART_MESSAGE_MOVE_MEMORY_1 = new uint8_t[5]{0xa5, 0x00, 0x02, 0xFD, 0xff};
static const uint8_t* REMOTE_UART_MESSAGE_MOVE_MEMORY_2 = new uint8_t[5]{0xa5, 0x00, 0x04, 0xFB, 0xff};

static const float REMOTE_UART_STOPPING_DISTANCE = 1.0;
static const int8_t REMOTE_UART_SEND_MESSAGE_COUNT = 3;

static const uint8_t DESK_UART_MESSAGE_LENGTH = 6U;
static const uint8_t DESK_UART_MESSAGE_START = 0x5a;

static const float MIN_HEIGHT = 65.5;
static const float MAX_HEIGHT = 129.5;

static const char* desktronic_operation_to_string(const DesktronicOperation operation)
{
    switch (operation)
    {
    case DESKTRONIC_OPERATION_IDLE:
        return "IDLE";
    case DESKTRONIC_OPERATION_RAISING:
        return "RAISING";
    case DESKTRONIC_OPERATION_LOWERING:
        return "LOWERING";
    case DESKTRONIC_OPERATION_DOWN:
        return "DOWN";
    case DESKTRONIC_OPERATION_UP:
        return "UP";
    case DESKTRONIC_OPERATION_MEMORY_1:
        return "MEMORY";
    case DESKTRONIC_OPERATION_MEMORY_2:
        return "MEMORY";
    default:
        return "UNKNOWN";
    }
}

static int segment_to_number(const uint8_t segment)
{
    switch (segment & 0x7f)
    {
    case SEGMENT_0:
        return 0;
    case SEGMENT_1:
        return 1;
    case SEGMENT_2:
        return 2;
    case SEGMENT_3:
        return 3;
    case SEGMENT_4:
        return 4;
    case SEGMENT_5:
        return 5;
    case SEGMENT_6:
        return 6;
    case SEGMENT_7:
        return 7;
    case SEGMENT_8:
        return 8;
    case SEGMENT_9:
        return 9;
    default:
        ESP_LOGW(TAG, "unknown digit: %02f", segment & 0x7f);
    }

    return -1;
}

void Desktronic::setup()
{
    if (move_pin_)
    {
        move_pin_->digital_write(false);
    }
}

void Desktronic::loop()
{
    read_desk_uart();

    if (current_operation == DesktronicOperation::DESKTRONIC_OPERATION_IDLE)
    {
        read_remote_uart();
        return;
    }

    else if (current_operation == DesktronicOperation::DESKTRONIC_OPERATION_UP)
    {
        move_up();
    }

    else if (current_operation == DesktronicOperation::DESKTRONIC_OPERATION_DOWN)
    {
        move_down();
    }

    else if (current_operation == DesktronicOperation::DESKTRONIC_OPERATION_MEMORY_1)
    {
        move_to_memory_1();
    }
    else if (current_operation == DesktronicOperation::DESKTRONIC_OPERATION_MEMORY_2)
    {
        move_to_memory_2();
    }

    else
    {
        move_to_target_height();
    }

}

void Desktronic::dump_config()
{
    ESP_LOGCONFIG(TAG, "Desktronic Desk");
    LOG_SENSOR("", "Height", height_sensor_);
    LOG_PIN("Up Pin: ", move_pin_);
    LOG_BINARY_SENSOR("  ", "Up", up_bsensor_);
    LOG_BINARY_SENSOR("  ", "Down", down_bsensor_);
    LOG_BINARY_SENSOR("  ", "Memory1", memory1_bsensor_);
    LOG_BINARY_SENSOR("  ", "Memory2", memory2_bsensor_);
    LOG_BINARY_SENSOR("  ", "Memory3", memory3_bsensor_);
}

void Desktronic::move_to(const float height_in_cm)
{
    if (height_in_cm < MIN_HEIGHT || height_in_cm > MAX_HEIGHT)
    {
        ESP_LOGE(TAG, "Moving: Height must be between %.0fs and %.0f cm", MIN_HEIGHT, MAX_HEIGHT);
        return;
    }

    target_height_ = height_in_cm;
    current_operation = must_move_up(height_in_cm) ? DESKTRONIC_OPERATION_RAISING : DESKTRONIC_OPERATION_LOWERING;
}

void Desktronic::move_to_from(const float height_in_cm, const float current_height)
{
    if (height_in_cm < MIN_HEIGHT || height_in_cm > MAX_HEIGHT)
    {
        ESP_LOGE(TAG, "Moving: Height must be between %.0fs and %.0f cm", MIN_HEIGHT, MAX_HEIGHT);
        return;
    }

    if (current_height < height_in_cm )
    {
        current_operation =  DESKTRONIC_OPERATION_UP;
    }
    else if (current_height < height_in_cm )
    {
        current_operation =  DESKTRONIC_OPERATION_DOWN;
    }
    else if (current_height == height_in_cm )
    {
        current_operation =  DESKTRONIC_OPERATION_IDLE;
    }

    target_height_ = height_in_cm;
}

void Desktronic::stop()
{
    if (move_pin_)
    {
        move_pin_->digital_write(false);
    }

    target_height_ = -1.0;
    current_operation = DESKTRONIC_OPERATION_IDLE;
    ESP_LOGI(TAG, "Stopped");
}

void Desktronic::move_up()
{
    if (move_pin_)
    {
        move_pin_->digital_write(false);
    }

    ESP_LOGV(TAG, "Moving: Forced Up");
    current_operation = DESKTRONIC_OPERATION_UP;

    for (int i = 0; i < REMOTE_UART_SEND_MESSAGE_COUNT; i++)
    {
        remote_uart_->write_array(REMOTE_UART_MESSAGE_MOVE_UP, REMOTE_UART_MESSAGE_LENGTH);
    }
}

void Desktronic::move_to_memory_1()
{
    if (move_pin_)
    {
        move_pin_->digital_write(false);
    }

    ESP_LOGD(TAG, "Moving: Memory 1");
    current_operation = DESKTRONIC_OPERATION_MEMORY_1;

    for (int i = 0; i < REMOTE_UART_SEND_MESSAGE_COUNT; i++)
    {
            remote_uart_->write_array(REMOTE_UART_MESSAGE_MOVE_MEMORY_1, REMOTE_UART_MESSAGE_LENGTH);
    }
}

void Desktronic::move_to_memory_2()
{
    if (move_pin_)
    {
        move_pin_->digital_write(false);
    }

    ESP_LOGD(TAG, "Moving: Memory 2");
    current_operation = DESKTRONIC_OPERATION_MEMORY_2;

    for (int i = 0; i < REMOTE_UART_SEND_MESSAGE_COUNT; i++)
    {
            remote_uart_->write_array(REMOTE_UART_MESSAGE_MOVE_MEMORY_2, REMOTE_UART_MESSAGE_LENGTH);
    }
}

void Desktronic::move_down()
{
    if (move_pin_)
    {
        move_pin_->digital_write(false);
    }

    ESP_LOGV(TAG, "Moving: Forced Down");
    current_operation = DESKTRONIC_OPERATION_DOWN;

    for (int i = 0; i < REMOTE_UART_SEND_MESSAGE_COUNT; i++)
    {
        remote_uart_->write_array(REMOTE_UART_MESSAGE_MOVE_DOWN, REMOTE_UART_MESSAGE_LENGTH);
    }
}

void Desktronic::read_remote_uart()
{
    if (!remote_uart_)
    {
        return;
    }

    uint8_t byte;
    while (remote_uart_->available())
    {
        remote_uart_->read_byte(&byte);

        // are we at the first-message and have to filter
        // some unnecessary bytes before the actual REMOTE_UART_MESSAGE_START?
        if (!is_remote_rx_uart_message_start_found)
        {
            if (byte != REMOTE_UART_MESSAGE_START)
            {
                continue;
            }

            reset_remote_buffer();
            is_remote_rx_uart_message_start_found = true;

            continue;
        }

        remote_buffer_.push_back(byte);

        // -1, because of the start byte
        // important for the right order of the bytes
        if (remote_buffer_.size() < REMOTE_UART_MESSAGE_LENGTH - 1)
        {
            continue;
        }

        is_remote_rx_uart_message_start_found = false;
        uint8_t* data = remote_buffer_.data();

        const uint8_t checksum = data[1] + data[2];
        if (checksum != data[3])
        {
            ESP_LOGD(TAG, "remote checksum mismatch: %02x (calculated checksum) != %02x (actual checksum)", checksum, data[3]);
            reset_remote_buffer();

            continue;
        }

        publish_remote_states(data[1]);
        reset_remote_buffer();
    }
}

void Desktronic::read_desk_uart()
{
    if (!desk_uart_)
    {
        return;
    }

    uint8_t byte;
    while (desk_uart_->available())
    {
        desk_uart_->read_byte(&byte);

        // are we at the first-message and have to filter
        // some unnecessary bytes before the actual DESK_UART_MESSAGE_START?
        if (!is_desk_rx_uart_message_start_found)
        {
            if (byte != DESK_UART_MESSAGE_START)
            {
                continue;
            }

            reset_desk_buffer();
            is_desk_rx_uart_message_start_found = true;

            continue;
        }

        desk_buffer_.push_back(byte);

        // -1, because of the start byte
        // important for the right order of the bytes
        if (desk_buffer_.size() < DESK_UART_MESSAGE_LENGTH - 1)
        {
            continue;
        }

        is_desk_rx_uart_message_start_found = false;
        uint8_t* data = desk_buffer_.data();

        const uint8_t checksum = data[0] + data[1] + data[2] + data[3];
        if (checksum != data[4])
        {
            ESP_LOGD(TAG, "desk checksum mismatch: %02x (calculated checksum) != %02x (actual checksum)", checksum, data[4]);
            reset_desk_buffer();

            continue;
        }

        if (height_sensor_)
        {
            if (data[3] != 0x01)
            {
                ESP_LOGD(TAG, "unknown message type %02x must be 0x01", data[3]);
                break;
            }

            if ((data[0] | data[1] | data[2]) == 0x00)
            {
                break;
            }

            const int data0 = segment_to_number(data[0]);
            const int data1 = segment_to_number(data[1]);
            const int data2 = segment_to_number(data[2]);

            if (data0 < 0x00 || data1 < 0x00 || data2 < 0x00)
            {
                break;
            }

            float height = segment_to_number(data[0]) * 100 + segment_to_number(data[1]) * 10 + segment_to_number(data[2]);
            if (data[1] & 0x80)
            {
                height /= 10.0;
            }
            else{

            }
                current_height_ = height;
                height_sensor_->publish_state(height);

        }
        else {
            ESP_LOGE(TAG, "Unable to get height sensor value");
        }
        reset_desk_buffer();
    }
}

void Desktronic::publish_remote_states(const uint8_t data)
{
    if (up_bsensor_)
    {
        up_bsensor_->publish_state(data & MovingIdentifier::MOVING_IDENTIFIER_UP);
    }
    if (down_bsensor_)
    {
        down_bsensor_->publish_state(data & MovingIdentifier::MOVING_IDENTIFIER_DOWN);
    }
    if (memory1_bsensor_)
    {
        memory1_bsensor_->publish_state(data & MovingIdentifier::MOVING_IDENTIFIER_MEMORY_1);
    }
    if (memory2_bsensor_)
    {
        memory2_bsensor_->publish_state(data & MovingIdentifier::MOVING_IDENTIFIER_MEMORY_2);
    }
    if (memory3_bsensor_)
    {
        memory3_bsensor_->publish_state(data & MovingIdentifier::MOVING_IDENTIFIER_MEMORY_3);
    }
}

void Desktronic::reset_remote_buffer()
{
    remote_buffer_.clear();
    remote_buffer_.resize(0);
}

void Desktronic::reset_desk_buffer()
{
    desk_buffer_.clear();
    desk_buffer_.resize(0);
}

bool Desktronic::must_move_up(const float height_in_cm) const
{
    return current_height_ < height_in_cm;
}

void Desktronic::move_to_target_height()
{
    if (!move_pin_)
    {
        ESP_LOGE(TAG, "Moving: Move pin is not configured");
        return;
    }

    if (!remote_uart_)
    {
        ESP_LOGE(TAG, "Moving: Remote UART is not configured");
        return;
    }

    if (!isCurrentHeightValid())
    {
        ESP_LOGE(TAG, "Moving: Height must be between %.0fs and %.0f cm", MIN_HEIGHT, MAX_HEIGHT);
        move_pin_->digital_write(false);
        current_operation = DesktronicOperation::DESKTRONIC_OPERATION_IDLE;

        return;
    }

    move_pin_->digital_write(true);
    switch (current_operation)
    {
    case DesktronicOperation::DESKTRONIC_OPERATION_RAISING:
        ESP_LOGV(TAG, "Moving: Up");
        for (int i = 0; i < REMOTE_UART_SEND_MESSAGE_COUNT; i++)
        {
            remote_uart_->write_array(REMOTE_UART_MESSAGE_MOVE_UP, REMOTE_UART_MESSAGE_LENGTH);
        }

        break;
    case DesktronicOperation::DESKTRONIC_OPERATION_LOWERING:
        ESP_LOGV(TAG, "Moving: Down");
        for (int i = 0; i < REMOTE_UART_SEND_MESSAGE_COUNT; i++)
        {
            remote_uart_->write_array(REMOTE_UART_MESSAGE_MOVE_DOWN, REMOTE_UART_MESSAGE_LENGTH);
        }

        break;
    default:
        return;
    }

    if (isCurrentHeightInTargetBoundaries())
    {
        ESP_LOGI(TAG, "Moving: Finished");
        move_pin_->digital_write(false);
        current_operation = DesktronicOperation::DESKTRONIC_OPERATION_IDLE;
    }
}

bool Desktronic::isCurrentHeightValid() const
{
    return (current_height_ >= MIN_HEIGHT) || (current_height_ <= MAX_HEIGHT);
}

bool Desktronic::isCurrentHeightInTargetBoundaries() const
{
    return (current_height_ >= target_height_ - REMOTE_UART_STOPPING_DISTANCE) &&
           (current_height_ <= target_height_ + REMOTE_UART_STOPPING_DISTANCE);
}

} // namespace desktronic
} // namespace esphome