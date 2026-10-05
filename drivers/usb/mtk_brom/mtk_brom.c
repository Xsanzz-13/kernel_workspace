#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

#define MTK_BROM_VENDOR  0x0e8d
#define MTK_BROM_PRODUCT 0x0003

struct mtk_brom {
	struct usb_device *udev;
	struct usb_interface *interface;

	unsigned int bulk_in;
	unsigned int bulk_out;
	unsigned int bulk_in_maxpacket;
	unsigned int bulk_out_maxpacket;
};

static int mtk_brom_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct mtk_brom *dev;
	int i;

	dev_info(&interface->dev,
		 "MTK BROM: detected %04x:%04x interface=%d\n",
		 le16_to_cpu(udev->descriptor.idVendor),
		 le16_to_cpu(udev->descriptor.idProduct),
		 interface->cur_altsetting->desc.bInterfaceNumber);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->udev = usb_get_dev(udev);
	dev->interface = interface;

	iface_desc = interface->cur_altsetting;

	dev_info(&interface->dev,
		 "MTK BROM: class=%02x subclass=%02x protocol=%02x endpoints=%u\n",
		 iface_desc->desc.bInterfaceClass,
		 iface_desc->desc.bInterfaceSubClass,
		 iface_desc->desc.bInterfaceProtocol,
		 iface_desc->desc.bNumEndpoints);

	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint = &iface_desc->endpoint[i].desc;

		dev_info(&interface->dev,
			 "MTK BROM: EP 0x%02x attr=0x%02x maxpacket=%u\n",
			 endpoint->bEndpointAddress,
			 endpoint->bmAttributes,
			 usb_endpoint_maxp(endpoint));

		if (usb_endpoint_is_bulk_in(endpoint)) {
			dev->bulk_in = endpoint->bEndpointAddress;
			dev->bulk_in_maxpacket =
				usb_endpoint_maxp(endpoint);
		}

		if (usb_endpoint_is_bulk_out(endpoint)) {
			dev->bulk_out = endpoint->bEndpointAddress;
			dev->bulk_out_maxpacket =
				usb_endpoint_maxp(endpoint);
		}
	}

	usb_set_intfdata(interface, dev);

	dev_info(&interface->dev,
		 "MTK BROM: probe successful\n");

	return 0;
}

static void mtk_brom_disconnect(struct usb_interface *interface)
{
	struct mtk_brom *dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	dev_info(&interface->dev,
		 "MTK BROM: disconnect\n");

	usb_put_dev(dev->udev);
	kfree(dev);
}

static const struct usb_device_id mtk_brom_ids[] = {
	{
		USB_DEVICE_INTERFACE_NUMBER(
			MTK_BROM_VENDOR,
			MTK_BROM_PRODUCT,
			0
		)
	},
	{ }
};

MODULE_DEVICE_TABLE(usb, mtk_brom_ids);

static struct usb_driver mtk_brom_driver = {
	.name       = "mtk_brom",
	.probe      = mtk_brom_probe,
	.disconnect = mtk_brom_disconnect,
	.id_table   = mtk_brom_ids,
};

module_usb_driver(mtk_brom_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xsanzz");
MODULE_DESCRIPTION("MediaTek BROM USB driver");
