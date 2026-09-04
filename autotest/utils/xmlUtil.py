from xml.etree.ElementTree import Element
from xml.etree.ElementTree import SubElement
from xml.etree.ElementTree import ElementTree


def generateConfig(port, appId, userAId, userBId, channelId1, channelId2, fileName):
    generateXML(port, appId, userAId, userBId, channelId1, channelId2, fileName)

    from xml.etree import ElementTree

    tree = ElementTree.parse(fileName)  # 解析xml文件
    root = tree.getroot()  # 得到根元素，Element类
    fortmatXml(root, '\t', '\n')  # 执行标签format方法

    tree.write(fileName, encoding='utf-8')


def generateXML(port, appid, userAId, userBId, channelId1, channelId2, fileName):
    # generate root node
    config = Element('Config')

    common = SubElement(config, 'Common')

    localHttpServerPort = SubElement(common, 'localHttpServerPort')
    localHttpServerPort.text = port

    appId = SubElement(common, 'appId')
    appId.text = appid

    UserA = SubElement(config, 'UserA')
    userName = SubElement(UserA, 'userName')
    userName.text = '1311ehiked'
    userPassword = SubElement(UserA, 'userPassword')
    userPassword.text = '0fwfpvkbv'
    UID = SubElement(UserA, 'UID')
    UID.text = userAId
    nickName = SubElement(UserA, 'nickName')
    nickName.text = '1311ehiked'

    UserB = SubElement(config, 'UserB')
    userName = SubElement(UserB, 'userName')
    userName.text = 'z58olbwz7v'
    userPassword = SubElement(UserB, 'userPassword')
    userPassword.text = 'gitvc4ybz'
    UID = SubElement(UserB, 'UID')
    UID.text = userBId
    nickName = SubElement(UserB, 'nickName')
    nickName.text = 'z58olbwz7v'

    Channel_1 = SubElement(config, 'Channel_1')
    channel_ID = SubElement(Channel_1, 'channel_ID')
    channel_ID.text = channelId1
    channel_Name = SubElement(Channel_1, 'channel_Name')
    channel_Name.text = 'autotest'
    subChannel_1 = SubElement(Channel_1, 'subChannel_1')
    subChannel_1.text = 'sub1'
    subChannel_2 = SubElement(Channel_1, 'subChannel_2')
    subChannel_2.text = 'sub2'
    subChannel_1_1 = SubElement(Channel_1, 'subChannel_1_1')
    subChannel_1_1.text = 'sub1sub1'
    subChannel_1_2 = SubElement(Channel_1, 'subChannel_1_2')
    subChannel_1_2.text = 'sub1sub2'

    Channel_2 = SubElement(config, 'Channel_2')
    channel_ID = SubElement(Channel_2, 'channel_ID')
    channel_ID.text = channelId2
    channel_Name = SubElement(Channel_2, 'channel_Name')
    channel_Name.text = 'freeModeChannel'

    tree = ElementTree(config)

    # write out xml data
    tree.write(fileName, encoding='utf-8')


# elemnt为传进来的Elment类，参数indent用于缩进，newline用于换行
def fortmatXml(element, indent, newline, level=0):
    # 判断element是否有子元素
    if element:
        # 如果element的text没有内容
        if element.text == None or element.text.isspace():
            element.text = newline + indent * (level + 1)
        else:
            element.text = newline + indent * (level + 1) + element.text.strip() + newline + indent * (level + 1)
            # 此处两行如果把注释去掉，Element的text也会另起一行
    # else:
    # element.text = newline + indent * (level + 1) + element.text.strip() + newline + indent * level
    temp = list(element)  # 将elemnt转成list
    for subelement in temp:
        # 如果不是list的最后一个元素，说明下一个行是同级别元素的起始，缩进应一致
        if temp.index(subelement) < (len(temp) - 1):
            subelement.tail = newline + indent * (level + 1)
        else:  # 如果是list的最后一个元素， 说明下一行是母元素的结束，缩进应该少一个
            subelement.tail = newline + indent * level
            # 对子元素进行递归操作
        fortmatXml(subelement, indent, newline, level=level + 1)


if __name__ == '__main__':
    pass
    # generateConfig('1234565', '567', '678', '321', '123')
