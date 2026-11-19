import esphome.codegen as cg
from esphome.components import climate, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

CONF_WIFI_ICON = "wifi_icon"
CONF_SNIFFER = "sniffer"
CONF_LINK_TIMEOUT = "link_timeout"

mill_ns = cg.esphome_ns.namespace("mill")
MillClimate = mill_ns.class_("MillClimate", uart.UARTDevice, climate.Climate, cg.Component)

CONFIG_SCHEMA = (
    climate.climate_schema(MillClimate)
    .extend(
        {
            # Vis WiFi- og Home Assistant-tilkobling på WiFi-ikonet i displayet
            cv.Optional(CONF_WIFI_ICON, default=False): cv.boolean,
            # Logg alle rammer i hex på INFO-nivå, og hvilke statusbytes som endret seg
            cv.Optional(CONF_SNIFFER, default=False): cv.boolean,
            # Varsle når ovnen ikke har sendt noe på så lenge (C9 kommer ca. hvert minutt). 0s slår av.
            cv.Optional(CONF_LINK_TIMEOUT, default="3min"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "mill", baud_rate=9600, require_tx=True, require_rx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)

    cg.add(var.set_wifi_icon(config[CONF_WIFI_ICON]))
    cg.add(var.set_sniffer(config[CONF_SNIFFER]))
    cg.add(var.set_link_timeout(config[CONF_LINK_TIMEOUT]))
