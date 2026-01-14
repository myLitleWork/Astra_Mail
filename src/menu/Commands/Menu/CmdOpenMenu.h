#pragma once
#include "menu/Command.h"
#include "menu/Menu.h"

#include <utility>
#include <functional>

namespace astra_mail
{
    class CmdOpenMenu : public ICommand
    {
        std::string title_;

    public:
        CmdOpenMenu(std::function<std::unique_ptr<Menu>()> factory,
                    std::string title)
                : factory_(std::move(factory)), title_(std::move(title)) {}

        CommandResult execute() override;


        [[nodiscard]] std::string title() const override;


    private:
        std::function<std::unique_ptr<Menu>()> factory_;
    };
};


