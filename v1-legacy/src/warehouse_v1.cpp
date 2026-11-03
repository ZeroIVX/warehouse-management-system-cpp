#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <conio.h>
#include <iomanip>
#include <ctime>

using namespace std;

// 商品结构体
struct Item {
    int id;
    string name;
    int quantity;
    double price;
};

struct History {
    string type;     // 操作类型
    int itemId;      // 商品ID
    time_t timestamp; // 时间戳
};
// UTF-8转本地编码（GBK）
string UTF8ToGBK(const string& utf8Str) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    wchar_t* wbuf = new wchar_t[wlen];
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wbuf, wlen);

    int len = WideCharToMultiByte(CP_ACP, 0, wbuf, -1, NULL, 0, NULL, NULL);
    char* buf = new char[len];
    WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, len, NULL, NULL);

    string result(buf);
    delete[] wbuf;
    delete[] buf;
    return result;
}

// 本地编码转UTF-8
string GBKToUTF8(const string& gbkStr) {
    int wlen = MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), -1, NULL, 0);
    wchar_t* wbuf = new wchar_t[wlen];
    MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), -1, wbuf, wlen);

    int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
    char* buf = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, len, NULL, NULL);

    string result(buf);
    delete[] wbuf;
    delete[] buf;
    return result;
}

vector<Item> items;
vector<History> historyLog;
const string FILENAME = "warehouse.dat";
const string HISTORY_FILENAME = "history.dat";


// 设置控制台颜色
void SetColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

// 保存数据到文件
void SaveData() {
    ofstream file(FILENAME, ios::binary);
    if (!file) {
        cerr << "无法打开文件进行保存！";
        return;
    }

    // 写入数据头（版本信息）
    const char* header = "WHv1";
    file.write(header, 4);

    // 写入商品数量
    size_t count = items.size();
    file.write(reinterpret_cast<char*>(&count), sizeof(count));

    // 写入每个商品数据
    for (const auto& item : items) {
        size_t nameLen = item.name.size();
        file.write(reinterpret_cast<const char*>(&item.id), sizeof(item.id));
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(item.name.c_str(), nameLen);
        file.write(reinterpret_cast<const char*>(&item.quantity), sizeof(item.quantity));
        file.write(reinterpret_cast<const char*>(&item.price), sizeof(item.price));
    }

    file.close();
}

// 从文件加载数据
void LoadData() {
    ifstream file(FILENAME, ios::binary);
    if (!file) return;

    // 验证文件头
    char header[5] = { 0 };
    file.read(header, 4);
    if (string(header) != "WHv1") {
        cerr << "无效的数据文件格式！";
        return;
    }

    // 读取商品数量
    size_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    // 读取每个商品数据
    for (size_t i = 0; i < count; ++i) {
        Item item;
        size_t nameLen;

        file.read(reinterpret_cast<char*>(&item.id), sizeof(item.id));
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));

        char* buffer = new char[nameLen + 1];
        file.read(buffer, nameLen);
        buffer[nameLen] = '\0';
        item.name = buffer;
        delete[] buffer;

        file.read(reinterpret_cast<char*>(&item.quantity), sizeof(item.quantity));
        file.read(reinterpret_cast<char*>(&item.price), sizeof(item.price));

        items.push_back(item);
    }

    file.close();
}

// 显示菜单
void ShowMenu() {
    system("cls");
    SetColor(14);
    cout << "=== 仓库管理系统 ===" << endl;
    SetColor(7);
    cout << "1. 添加商品" << endl;
    cout << "2. 商品列表" << endl;
    cout << "3. 搜索商品" << endl;
    cout << "4. 修改商品" << endl;
    cout << "5. 删除商品" << endl;
    cout << "6. 操作日志" << endl; 
    cout << "7. 导出数据" << endl;
    cout << "8. 导入数据" << endl;
    cout << "9. 退出系统" << endl;
    cout << "请选择操作：";
}

// 添加商品
void AddItem() {
    Item newItem;
    cout << "\n输入商品ID: ";
    cin >> newItem.id;

    // 检查ID是否已存在
    for (const auto& item : items) {
        if (item.id == newItem.id) {
            SetColor(12);
            cout << "该ID已存在！" << endl;
            SetColor(7);
            cout << "\n按任意键返回...";
            _getch();
            return;
        }
    }

    cout << "输入商品名称: ";
    cin.ignore();
    getline(cin, newItem.name);
    cout << "输入商品数量: ";
    cin >> newItem.quantity;
    cout << "输入商品价格: ";
    cin >> newItem.price;

    items.push_back(newItem);
    SaveData();

    SetColor(10);
    cout << "商品添加成功！" << endl;
    History addHistory;
    addHistory.type = "添加";
    addHistory.itemId = newItem.id;
    addHistory.timestamp = time(nullptr);
    historyLog.push_back(addHistory);
    SetColor(7);
    cout << "\n按任意键返回...";
    _getch();
}

// 显示所有商品
void ShowAllItems() {
    system("cls");
    SetColor(11);
    cout << left << setw(8) << "ID"
        << setw(20) << "名称"
        << setw(10) << "数量"
        << setw(10) << "价格" << endl;
    SetColor(7);

    for (const auto& item : items) {
        cout << left << setw(8) << item.id
            << setw(20) << item.name
            << setw(10) << item.quantity
            << fixed << setprecision(2) << item.price << endl;
    }

    cout << "\n按任意键返回...";
    _getch();
}

// 搜索商品
void SearchItem() {
    int choice;
    cout << "\n1. 按ID搜索\n2. 按名称搜索\n选择搜索方式：";
    cin >> choice;

    if (choice == 1) {
        int id;
        cout << "输入要搜索的商品ID: ";
        cin >> id;

        for (const auto& item : items) {
            if (item.id == id) {
                SetColor(10);
                cout << "找到商品：" << endl;
                cout << "ID: " << item.id << endl;
                cout << "名称: " << item.name << endl;
                cout << "数量: " << item.quantity << endl;
                cout << "价格: " << item.price << endl;
                SetColor(7);
                cout << "\n按任意键返回...";
                _getch();
                return;
            }
        }
    }
    else if (choice == 2) {
        string name;
        cout << "输入要搜索的商品名称: ";
        cin.ignore();
        getline(cin, name);

        for (const auto& item : items) {
            if (item.name == name) {
                SetColor(10);
                cout << "找到商品：" << endl;
                cout << "ID: " << item.id << endl;
                cout << "名称: " << item.name << endl;
                cout << "数量: " << item.quantity << endl;
                cout << "价格: " << item.price << endl;
                SetColor(7);
                cout << "\n按任意键返回...";
                _getch();
                return;
            }
        }
    }

    SetColor(12);
    cout << "未找到匹配的商品！" << endl;
    SetColor(7);
    Sleep(1000);
}

//修改商品
void ModifyItem() {
    system("cls");
    SetColor(14);
    cout << "=== 修改商品 ===" << endl;
    SetColor(7);

    int id;
    cout << "输入要修改的商品ID: ";
    cin >> id;

    bool found = false;
    for (auto& item : items) {
        if (item.id == id) {
            found = true;

            // 显示当前信息
            cout << "\n当前商品信息：" << endl;
            cout << "ID: " << item.id << endl;
            cout << "名称: " << item.name << endl;
            cout << "数量: " << item.quantity << endl;
            cout << "价格: " << item.price << endl;

            cin.ignore(); // 清除输入缓冲区中的换行符

            // 修改名称
            string newName;
            cout << "\n输入新名称（当前：" << item.name << "，直接回车保持不变）: ";
            getline(cin, newName);
            if (!newName.empty()) {
                item.name = newName;
            }

            // 修改数量
            string qtyInput;
            cout << "输入新数量（当前：" << item.quantity << "，直接回车保持不变）: ";
            getline(cin, qtyInput);
            if (!qtyInput.empty()) {
                try {
                    int newQty = stoi(qtyInput);
                    item.quantity = newQty;
                }
                catch (...) {
                    SetColor(12);
                    cout << "无效的数量输入，保持原值。" << endl;
                    SetColor(7);
                }
            }

            // 修改价格
            string priceInput;
            cout << "输入新价格（当前：" << item.price << "，直接回车保持不变）: ";
            getline(cin, priceInput);
            if (!priceInput.empty()) {
                try {
                    double newPrice = stod(priceInput);
                    item.price = newPrice;
                }
                catch (...) {
                    SetColor(12);
                    cout << "无效的价格输入，保持原值。" << endl;
                    SetColor(7);
                }
            }

            SaveData();

            SetColor(10);
            cout << "\n商品信息修改成功！" << endl;
            History modHistory;
            modHistory.type = "修改";
            modHistory.itemId = id;
            modHistory.timestamp = time(nullptr);
            historyLog.push_back(modHistory);

            SetColor(7);
            break;
        }
    }

    if (!found) {
        SetColor(12);
        cout << "未找到ID为 " << id << " 的商品！" << endl;
        SetColor(7);
    }

    cout << "\n按任意键返回...";
    _getch();
}

// 删除商品
void DeleteItem() {
    system("cls");
    SetColor(14);
    cout << "=== 删除商品 ===" << endl;
    SetColor(7);

    int id;
    cout << "输入要删除的商品ID: ";
    cin >> id;

    auto it = items.begin();
    bool found = false;
    for (; it != items.end(); ++it) {
        if (it->id == id) {
            found = true;
            break;
        }
    }

    if (found) {
        // 显示商品信息
        cout << "\n找到商品：" << endl;
        cout << "ID: " << it->id << endl;
        cout << "名称: " << it->name << endl;
        cout << "数量: " << it->quantity << endl;
        cout << "价格: " << it->price << endl;

        // 确认
        char confirm;
        cout << "\n确定要删除吗？(y/n): ";
        cin >> confirm;

        if (tolower(confirm) == 'y') {
            items.erase(it);
            SaveData();
            SetColor(10);
            cout << "商品删除成功！" << endl;
            History delHistory;
            delHistory.type = "删除";
            delHistory.itemId = id;
            delHistory.timestamp = time(nullptr);
            historyLog.push_back(delHistory);
        }
        else {
            cout << "取消删除操作。" << endl;
        }
        SetColor(7);
    }
    else {
        SetColor(12);
        cout << "未找到ID为 " << id << " 的商品！" << endl;
        SetColor(7);
    }

    cout << "\n按任意键返回...";
    _getch();
}

//保存历史记录
void SaveHistory() {
    ofstream file(HISTORY_FILENAME, ios::binary);
    if (!file) {
        cerr << "无法保存历史记录！";
        return;
    }

    size_t count = historyLog.size();
    file.write(reinterpret_cast<char*>(&count), sizeof(count));

    for (const auto& entry : historyLog) {
        size_t typeLen = entry.type.size();
        file.write(reinterpret_cast<const char*>(&typeLen), sizeof(typeLen));
        file.write(entry.type.c_str(), typeLen);
        file.write(reinterpret_cast<const char*>(&entry.itemId), sizeof(entry.itemId));
        file.write(reinterpret_cast<const char*>(&entry.timestamp), sizeof(entry.timestamp));
    }

    file.close();
}

//加载历史记录
void LoadHistory() {
    ifstream file(HISTORY_FILENAME, ios::binary);
    if (!file) return;

    size_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    for (size_t i = 0; i < count; ++i) {
        History entry;
        size_t typeLen;

        file.read(reinterpret_cast<char*>(&typeLen), sizeof(typeLen));
        char* buffer = new char[typeLen + 1];
        file.read(buffer, typeLen);
        buffer[typeLen] = '\0';
        entry.type = buffer;
        delete[] buffer;

        file.read(reinterpret_cast<char*>(&entry.itemId), sizeof(entry.itemId));
        file.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));

        historyLog.push_back(entry);
    }

    file.close();
}

//显示历史记录
void ShowHistory() {
    system("cls");
    SetColor(14);
    cout << "=== 操作历史记录 ===" << endl;
    SetColor(7);

    if (historyLog.empty()) {
        cout << "暂无历史记录" << endl;
    }
    else {
        SetColor(11);
        cout << left << setw(20) << "时间"
            << setw(10) << "操作"
            << setw(10) << "商品ID" << endl;
        SetColor(7);

        for (auto it = historyLog.rbegin(); it != historyLog.rend(); ++it) {
            tm* lt = localtime(&it->timestamp);
            char timeStr[20];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", lt);

            cout << left << setw(20) << timeStr
                << setw(10) << it->type
                << setw(10) << it->itemId << endl;
        }
    }

    cout << "\n按任意键返回...";
    _getch();
}

//导出数据
void ExportToCSV() {
    system("cls");
    SetColor(14);
    cout << "=== 导出数据 ===" << endl;
    SetColor(7);

    string filename;
    cout << "输入导出文件名（例：data.csv）：";
    cin >> filename;

    ofstream outFile(filename);
    if (!outFile) {
        SetColor(12);
        cout << "无法创建文件！" << endl;
        SetColor(7);
        _getch();
        return;
    }

    // 以二进制模式写入UTF-8 BOM
    outFile << "\xEF\xBB\xBF";  // UTF-8 BOM

    // 写入数据
    for (const auto& item : items) {
        string utf8Name = GBKToUTF8(item.name);
        outFile << item.id << ","
            << "\"" << utf8Name << "\","
            << item.quantity << ","
            << fixed << setprecision(2) << item.price << "\n";
    }

    outFile.close();

    SetColor(10);
    cout << "成功导出 " << items.size() << " 条记录到 " << filename << endl;
    SetColor(7);
    cout << "\n按任意键返回...";
    _getch();
}

//导入数据
void ImportFromCSV() {
    system("cls");
    SetColor(14);
    cout << "=== 导入数据 ===" << endl;
    SetColor(7);

    string filename;
    cout << "输入要导入的文件名：";
    cin >> filename;

    // 以二进制模式打开文件
    ifstream inFile(filename, ios::binary);
    if (!inFile) {
        SetColor(12);
        cout << "文件不存在或无法打开！" << endl;
        SetColor(7);
        _getch();
        return;
    }

    // 检测并跳过UTF-8 BOM
    string bom(3, '\0');
    inFile.read(&bom[0], 3);
    if (bom != "\xEF\xBB\xBF") {
        inFile.seekg(0);  // 没有BOM则重置文件指针
    }

    string line;
    int imported = 0, skipped = 0;
    const int MAX_LINE = 1000;  // 防止恶意长行

    // 逐行读取处理
    while (inFile.good()) {
        // 安全读取行内容
        char buffer[MAX_LINE];
        inFile.getline(buffer, MAX_LINE);
        line = buffer;

        // 处理Windows换行符
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // 跳过空行
        if (line.empty()) continue;

        // 初始化新商品对象
        Item newItem;
        bool valid = true;
        size_t pos = 0;

        try {
            // 解析ID
            size_t end = line.find(',', pos);
            if (end == string::npos) throw invalid_argument("格式错误");
            newItem.id = stoi(line.substr(pos, end - pos));
            pos = end + 1;

            // 解析商品名称（处理带引号情况）
            string utf8Name;
            if (pos < line.length() && line[pos] == '"') {
                pos++;  // 跳过起始引号
                end = line.find('"', pos);
                if (end == string::npos) throw invalid_argument("引号不匹配");
                utf8Name = line.substr(pos, end - pos);
                pos = end + 2;  // 跳过引号和逗号
            }
            else {
                end = line.find(',', pos);
                if (end == string::npos) throw invalid_argument("格式错误");
                utf8Name = line.substr(pos, end - pos);
                pos = end + 1;
            }
            newItem.name = UTF8ToGBK(utf8Name);  // 编码转换

            // 解析库存数量
            end = line.find(',', pos);
            if (end == string::npos) throw invalid_argument("格式错误");
            newItem.quantity = stoi(line.substr(pos, end - pos));
            pos = end + 1;

            // 解析价格
            newItem.price = stod(line.substr(pos));
        }
        catch (...) {
            valid = false;
            skipped++;
            continue;
        }

        // 验证商品数据
        if (newItem.id <= 0 || newItem.quantity < 0 || newItem.price <= 0) {
            valid = false;
            skipped++;
            continue;
        }

        // 检查ID冲突
        bool exists = false;
        auto conflictIt = items.end();
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it->id == newItem.id) {
                exists = true;
                conflictIt = it;
                break;
            }
        }

        if (exists) {
            // 交互式冲突解决
            SetColor(14);
            cout << "\n发现重复ID：" << newItem.id << endl;
            cout << "现有商品：" << conflictIt->name
                << " 新商品：" << newItem.name << endl;
            cout << "是否覆盖？(y-覆盖/n-跳过/c-取消导入): ";

            char choice;
            cin >> choice;
            cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n'); // 清空缓冲区

            switch (tolower(choice)) {
            case 'y':
                *conflictIt = newItem;
                imported++;
                break;
            case 'n':
                skipped++;
                break;
            case 'c':
                inFile.close();
                SetColor(7);
                cout << "\n导入已取消" << endl;
                _getch();
                return;
            default:
                SetColor(12);
                cout << "无效选择，默认跳过" << endl;
                SetColor(7);
                skipped++;
            }
        }
        else {
            items.push_back(newItem);
            imported++;
        }
    }

    inFile.close();
    SaveData();  // 保存到二进制文件

    // 显示导入结果
    SetColor(11);
    cout << "\n导入完成！" << endl;
    cout << "════════════════" << endl;
    cout << "成功导入: " << imported << " 条" << endl;
    cout << "跳过记录: " << skipped << " 条" << endl;
    SetColor(7);
    cout << "\n按任意键返回...";
    _getch();
}


int main() {
    SetConsoleOutputCP(CP_ACP);
    SetConsoleCP(CP_ACP);
    SetConsoleTitle(L"仓库管理系统");
    LoadData();
    LoadHistory();  // 加载历史记录

    while (true) {
        ShowMenu();
        int choice;
        cin >> choice;

        switch (choice) {
        case 1: AddItem(); break;
        case 2: ShowAllItems(); break;
        case 3: SearchItem(); break;
        case 4: ModifyItem(); break;
        case 5: DeleteItem(); break;
        case 6: ShowHistory(); break;
        case 7: ExportToCSV(); break;
        case 8: ImportFromCSV(); break;
        case 9:  // 退出
            SaveData();
            SaveHistory();
            exit(0);
        default:
            SetColor(12);
            cout << "无效的选项！" << endl;
            SetColor(7);
            Sleep(1000);
        }
    }
    return 0;
}